#include "niki/meta/orchestrator/compile_pipeline.hpp"
#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/ir/lower_to_chunk.hpp"
#include "niki/l0_core/ir/verify.hpp"
#include "niki/l0_core/semantic/type_checker.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/vm/object.hpp"
#include "niki/l0_core/vm/value.hpp"
#include "niki/meta/precompile/precompile_pipeline.hpp"
#include <filesystem>
#include <limits>
#include <sstream>
#include <vector>

/** @meta_compile_pipeline_impl: 后端编译阶段实现
 * 该文件聚焦“单编译单元后端编译”：IR build、IR verify、IR lower、CompileModule 封装。
 * 这是从 orchestrator 主流程中下沉出的实现细节层，避免编排层承担过多实现职责。
 */
namespace niki::meta::orchestrator {
namespace {

struct DeclLocation {
    uint32_t line = 0;
    uint32_t column = 0;
};

/**
 * @brief 收集顶层声明节点列表。
 * @param unit 编译单元。
 * @return std::vector<syntax::ASTNodeIndex> 顶层声明节点集合。
 */
std::vector<syntax::ASTNodeIndex> collectTopLevelDecls(const GlobalCompilationUnit &unit) {
    std::vector<syntax::ASTNodeIndex> decls;
    if (!unit.root.isvalid()) {
        return decls;
    }
    const syntax::ASTNode &root_node = unit.pool.getNode(unit.root);
    if (root_node.type != syntax::NodeType::ModuleDecl && root_node.type != syntax::NodeType::ProgramRoot) {
        return decls;
    }
    syntax::ASTNodeIndex body_index =
        (root_node.type == syntax::NodeType::ModuleDecl) ? root_node.payload.module_decl.body : unit.root;
    const syntax::ASTNode &body_node = unit.pool.getNode(body_index);
    auto span = unit.pool.get_list(body_node.payload.list.elements);
    decls.assign(span.begin(), span.end());
    return decls;
}

/**
 * @brief 收集顶层 function/struct 名称到源码位置的映射。
 * @param unit 编译单元。
 * @return std::unordered_map<uint32_t, DeclLocation> 名称 sid 到声明位置映射。
 */
std::unordered_map<uint32_t, DeclLocation> collectTopDeclNameLocations(const GlobalCompilationUnit &unit) {
    std::unordered_map<uint32_t, DeclLocation> locations_by_name_id;
    auto decls = collectTopLevelDecls(unit);
    locations_by_name_id.reserve(decls.size());
    for (syntax::ASTNodeIndex decl_idx : decls) {
        if (!decl_idx.isvalid() || decl_idx.index >= unit.pool.locations.size()) {
            continue;
        }
        const syntax::ASTNode &decl_node = unit.pool.getNode(decl_idx);
        uint32_t name_id = std::numeric_limits<uint32_t>::max();
        if (decl_node.type == syntax::NodeType::FunctionDecl) {
            const auto &function_data = unit.pool.function_data[decl_node.payload.func_decl.function_index];
            name_id = function_data.name_id;
        } else if (decl_node.type == syntax::NodeType::StructDecl) {
            const auto &struct_data = unit.pool.struct_data[decl_node.payload.struct_decl.struct_index];
            name_id = struct_data.name_id;
        }
        if (name_id == std::numeric_limits<uint32_t>::max()) {
            continue;
        }
        const auto &loc = unit.pool.locations[decl_idx.index];
        locations_by_name_id[name_id] = DeclLocation{.line = loc.line, .column = loc.column};
    }
    return locations_by_name_id;
}

/**
 * @brief 将 IR verify issue 映射回源码 span。
 * @param unit 编译单元。
 * @param module_ir 模块 IR。
 * @param issue verify issue。
 * @return diagnostic::SourceSpan 最佳努力定位后的源码 span。
 */
diagnostic::SourceSpan buildVerifyIssueSourceSpan(const GlobalCompilationUnit &unit, const ir::ModuleIR &module_ir,
                                                  const ir::VerifyIssue &issue) {
    uint32_t line = 0;
    uint32_t column = 0;
    if (issue.func_idx < module_ir.funcs.size()) {
        const auto function_decl_locations = collectTopDeclNameLocations(unit);
        const ir::FuncRecord &function_record = module_ir.funcs[issue.func_idx];
        auto function_decl_iter = function_decl_locations.find(function_record.func_name_sid);
        if (function_decl_iter != function_decl_locations.end()) {
            line = function_decl_iter->second.line;
            column = function_decl_iter->second.column;
        }
    }
    return diagnostic::makeSourceSpan(unit.source_path, line, column);
}

/**
 * @brief 根据 lower 后函数列表生成模块初始化 chunk。
 * @param lowered_functions 已降级函数对象列表。
 * @param string_pool 模块字符串池。
 * @return Chunk 可执行初始化 chunk。
 */
Chunk makeInitChunkFromLoweredFunctions(const std::vector<vm::ObjFunction *> &lowered_functions,
                                        const std::vector<std::string> &string_pool) {
    Chunk init_chunk;
    init_chunk.string_pool = string_pool;
    init_chunk.max_register_slots = 1;
    init_chunk.constants.reserve(lowered_functions.size());
    for (vm::ObjFunction *function_object : lowered_functions) {
        const uint32_t constant_index = static_cast<uint32_t>(init_chunk.constants.size());
        init_chunk.constants.push_back(vm::Value::makeObject(function_object));
        if (constant_index <= std::numeric_limits<uint8_t>::max()) {
            init_chunk.code.push_back(vm::ToInt(vm::OPCODE::OP_DEFINE_GLOBAL));
            init_chunk.code.push_back(static_cast<uint8_t>(constant_index));
            init_chunk.lines.push_back(0);
            init_chunk.lines.push_back(0);
            init_chunk.columns.push_back(0);
            init_chunk.columns.push_back(0);
        } else {
            init_chunk.code.push_back(vm::ToInt(vm::OPCODE::OP_DEFINE_GLOBAL_W));
            init_chunk.code.push_back(static_cast<uint8_t>((constant_index >> 8) & 0xFF));
            init_chunk.code.push_back(static_cast<uint8_t>(constant_index & 0xFF));
            init_chunk.lines.push_back(0);
            init_chunk.lines.push_back(0);
            init_chunk.lines.push_back(0);
            init_chunk.columns.push_back(0);
            init_chunk.columns.push_back(0);
            init_chunk.columns.push_back(0);
        }
    }
    init_chunk.code.push_back(vm::ToInt(vm::OPCODE::OP_RETURN));
    init_chunk.lines.push_back(0);
    init_chunk.columns.push_back(0);
    return init_chunk;
}

/**
 * @brief 从 lower 后函数对象收集导出符号映射。
 * @param funcs 函数对象列表。
 * @return std::unordered_map<uint32_t, uint32_t> 导出表。
 */
std::unordered_map<uint32_t, uint32_t> collectExportsFromLoweredFunctions(const std::vector<vm::ObjFunction *> &funcs) {
    std::unordered_map<uint32_t, uint32_t> exports;
    exports.reserve(funcs.size());
    for (vm::ObjFunction *f : funcs) {
        if (f != nullptr) {
            exports[f->name_id] = f->name_id;
        }
    }
    return exports;
}

} // namespace

/**
 * @brief 执行单编译单元后端编译（IR build/verify/lower）。
 * @param unit 编译单元。
 * @param global_arena 全局类型 arena（接口保留）。
 * @param global_symbols 全局符号表（接口保留）。
 * @return std::expected<UnitCompileArtifact, diagnostic::DiagnosticBag> 成功返回后端产物，失败返回诊断。
 */
std::expected<UnitCompileArtifact, diagnostic::DiagnosticBag> compileUnitChunk(GlobalCompilationUnit &unit,
                                                                                GlobalTypeArena &global_arena,
                                                                                GlobalSymbolTable &global_symbols) {
    (void)global_arena;
    (void)global_symbols;
    ir::IRBuilder ir_builder;
    auto ir_result = ir_builder.build(unit);
    if (!ir_result.has_value()) {
        return std::unexpected(std::move(ir_result.error()));
    }
    ir::VerifyReport verify_report = ir::verifyModuleIRFlat(ir_result.value());
    if (!verify_report.ok()) {
        diagnostic::DiagnosticBag diagnostics;
        for (const auto &issue : verify_report.issues) {
            std::ostringstream verify_message;
            verify_message << "IR verify failed code=" << static_cast<uint16_t>(issue.error_code) << " func_idx=" << issue.func_idx
                           << " rel_block_idx=" << issue.rel_block_idx << " inst_idx=" << issue.inst_idx
                           << " message=" << issue.message;
            diagnostics.error(diagnostic::events::IRCode::VerifyFailed, verify_message.str(),
                              buildVerifyIssueSourceSpan(unit, ir_result.value(), issue));
        }
        return std::unexpected(std::move(diagnostics));
    }
    auto lower_result = ir::lowerModuleToChunk(ir_result.value());
    if (!lower_result.has_value()) {
        diagnostic::DiagnosticBag diagnostics;
        diagnostics.error(diagnostic::events::IRCode::LowerFailed, "IR lower failed: " + lower_result.error(),
                          diagnostic::makeSourceSpan(unit.source_path));
        return std::unexpected(std::move(diagnostics));
    }
    UnitCompileArtifact artifact;
    artifact.module_name = ir_result.value().module_name;
    artifact.init_chunk = makeInitChunkFromLoweredFunctions(lower_result.value().functions, ir_result.value().string_pool);
    artifact.exports = collectExportsFromLoweredFunctions(lower_result.value().functions);

    // 收集非函数导出符号（component/kits 等）
    for (const auto &sym : ir_result.value().syms) {
        if (sym.is_exported && sym.sym_kind != ir::SymKind::Func) {
            artifact.exported_sym_records.push_back(sym);
        }
    }

    return artifact;
}

/**
 * @brief 将后端中间工件封装为 linker 输入模块。
 * @param source_path 源文件路径。
 * @param artifact 后端产物。
 * @return linker::CompileModule 模块产物。
 */
linker::CompileModule buildCompileModule(std::string source_path, UnitCompileArtifact artifact) {
    linker::CompileModule module;
    // 优先取显式 module 名，回退到文件名 stem
    if (!artifact.module_name.empty()) {
        module.module_name = std::move(artifact.module_name);
    } else {
        module.module_name = std::filesystem::path(source_path).stem().string();
    }
    module.source_path = std::move(source_path);
    module.init_chunk = std::move(artifact.init_chunk);
    module.exports = std::move(artifact.exports);
    module.exported_symbols = std::move(artifact.exported_sym_records);
    return module;
}

/**
 * @brief 单编译单元后端流水线入口（无重复语义阶段）。
 * @param unit 编译单元。
 * @param global_arena 全局类型 arena。
 * @param global_symbols 全局符号表。
 * @return std::expected<linker::CompileModule, diagnostic::DiagnosticBag> 成功返回模块产物，失败返回诊断。
 */
std::expected<linker::CompileModule, diagnostic::DiagnosticBag> compileParsedBackend(GlobalCompilationUnit &unit,
                                                                                    GlobalTypeArena &global_arena,
                                                                                    GlobalSymbolTable &global_symbols) {
    auto artifact_result = compileUnitChunk(unit, global_arena, global_symbols);
    if (!artifact_result.has_value()) {
        return std::unexpected(std::move(artifact_result.error()));
    }
    return buildCompileModule(std::move(unit.source_path), std::move(artifact_result.value()));
}

/**
 * @brief 单编译单元完整流水线：预声明、模块可见性、类型检查、后端编译。
 * @note 通过临时 vector 调用 buildModuleSemanticContext，避免复制 GlobalCompilationUnit。
 */
std::expected<linker::CompileModule, diagnostic::DiagnosticBag> compileParsedUnit(GlobalCompilationUnit &unit,
                                                                                GlobalTypeArena &global_arena,
                                                                                GlobalSymbolTable &global_symbols) {
    auto predeclare_result = meta::precompile::predeclareSingleUnit(unit, global_arena, global_symbols);
    if (!predeclare_result.has_value()) {
        return std::unexpected(std::move(predeclare_result.error()));
    }

    std::vector<GlobalCompilationUnit> single_unit;
    single_unit.push_back(std::move(unit));

    auto context_result = meta::precompile::buildModuleSemanticContext(single_unit, global_symbols);
    if (!context_result.has_value()) {
        unit = std::move(single_unit[0]);
        return std::unexpected(std::move(context_result.error()));
    }

    semantic::TypeChecker checker;
    auto type_result =
        checker.check(single_unit[0].pool, single_unit[0].root, global_symbols, global_arena,
                      context_result.value().visible_per_unit[0]);
    if (!type_result.has_value()) {
        unit = std::move(single_unit[0]);
        return std::unexpected(std::move(type_result.error()));
    }

    auto backend_result = compileParsedBackend(single_unit[0], global_arena, global_symbols);
    unit = std::move(single_unit[0]);
    if (!backend_result.has_value()) {
        return std::unexpected(std::move(backend_result.error()));
    }
    return backend_result;
}

} // namespace niki::meta::orchestrator
