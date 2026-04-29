#include "niki/driver/driver.hpp"
#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/ir/lower_to_chunk.hpp"
#include "niki/l0_core/ir/verify.hpp"
#include "niki/l0_core/linker/linker.hpp"
#include "niki/l0_core/runtime/launcher.hpp"
#include "niki/l0_core/semantic/global_compilation.hpp"
#include "niki/l0_core/semantic/global_symbol_table.hpp"
#include "niki/l0_core/semantic/global_type_arena.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/semantic/type_checker.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/ast_payloads.hpp"
#include "niki/l0_core/syntax/global_interner.hpp"
#include "niki/l0_core/syntax/parser.hpp"
#include "niki/l0_core/syntax/scanner.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include "niki/l0_core/vm/chunk.hpp"
#include "niki/l0_core/vm/object.hpp"
#include "niki/l0_core/vm/value.hpp"
#include "niki/l0_core/vm/vm.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace niki::driver {
namespace fs = std::filesystem;

namespace {

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

std::unordered_map<uint32_t, uint32_t> collectExportsFromLoweredFunctions(
    const std::vector<vm::ObjFunction *> &lowered_functions) {
    std::unordered_map<uint32_t, uint32_t> exports;
    exports.reserve(lowered_functions.size());
    for (vm::ObjFunction *function_object : lowered_functions) {
        if (function_object == nullptr) {
            continue;
        }
        exports[function_object->name_id] = function_object->name_id;
    }
    return exports;
}

std::unordered_map<uint32_t, uint32_t> collectExportsFromUnitTopDecls(const GlobalCompilationUnit &unit) {
    std::unordered_map<uint32_t, uint32_t> exports;
    if (!unit.root.isvalid()) {
        return exports;
    }
    const syntax::ASTNode &root_node = unit.pool.getNode(unit.root);
    if (root_node.type != syntax::NodeType::ModuleDecl && root_node.type != syntax::NodeType::ProgramRoot) {
        return exports;
    }
    const syntax::ASTNodeIndex body_index =
        (root_node.type == syntax::NodeType::ModuleDecl) ? root_node.payload.module_decl.body : unit.root;
    const syntax::ASTNode &body_node = unit.pool.getNode(body_index);
    const auto decls = unit.pool.get_list(body_node.payload.list.elements);

    exports.reserve(decls.size());
    for (syntax::ASTNodeIndex decl_idx : decls) {
        if (!decl_idx.isvalid()) {
            continue;
        }
        const syntax::ASTNode &decl_node = unit.pool.getNode(decl_idx);
        if (decl_node.type == syntax::NodeType::FunctionDecl) {
            const auto &function_data = unit.pool.function_data[decl_node.payload.func_decl.function_index];
            exports[function_data.name_id] = function_data.name_id;
        } else if (decl_node.type == syntax::NodeType::StructDecl) {
            const auto &struct_data = unit.pool.struct_data[decl_node.payload.struct_decl.struct_index];
            exports[struct_data.name_id] = struct_data.name_id;
        }
    }
    return exports;
}

struct DeclLocation {
    uint32_t line = 0;
    uint32_t column = 0;
};

std::unordered_map<uint32_t, DeclLocation> collectTopDeclNameLocations(const GlobalCompilationUnit &unit) {
    std::unordered_map<uint32_t, DeclLocation> locations_by_name_id;
    if (!unit.root.isvalid()) {
        return locations_by_name_id;
    }
    const syntax::ASTNode &root_node = unit.pool.getNode(unit.root);
    if (root_node.type != syntax::NodeType::ModuleDecl && root_node.type != syntax::NodeType::ProgramRoot) {
        return locations_by_name_id;
    }
    const syntax::ASTNodeIndex body_index =
        (root_node.type == syntax::NodeType::ModuleDecl) ? root_node.payload.module_decl.body : unit.root;
    const syntax::ASTNode &body_node = unit.pool.getNode(body_index);
    const auto decls = unit.pool.get_list(body_node.payload.list.elements);

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

        if (issue.rel_block_idx < function_record.block_span.count) {
            const uint32_t absolute_block_index = function_record.block_span.begin + issue.rel_block_idx;
            if (absolute_block_index < module_ir.blocks.size()) {
                const ir::BlockRecord &block_record = module_ir.blocks[absolute_block_index];
                if (issue.inst_idx < block_record.inst_span.count) {
                    const uint32_t absolute_inst_index = block_record.inst_span.begin + issue.inst_idx;
                    if (absolute_inst_index < module_ir.insts.size()) {
                        const uint32_t source_line = module_ir.insts.src_line[absolute_inst_index];
                        const uint32_t source_col = module_ir.insts.src_col[absolute_inst_index];
                        if (source_line != 0 || source_col != 0) {
                            line = source_line;
                            column = source_col;
                        }
                    }
                }
            }
        }
    }
    return diagnostic::makeSourceSpan(unit.source_path, line, column);
}

} // namespace

static diagnostic::DiagnosticBag makeDriverError(diagnostic::events::DriverCode code, std::string message,
                                                 std::string file = "") {
    diagnostic::DiagnosticBag bag;
    bag.error(code, std::move(message), diagnostic::makeSourceSpan(std::move(file)));
    return bag;
}

static semantic::NKType resolvePredeclareType(const GlobalCompilationUnit &unit, syntax::ASTNodeIndex type_expr_idx,
                                              const GlobalSymbolTable &global_symbols,
                                              diagnostic::DiagnosticBag &diagnostics, uint32_t line, uint32_t column) {
    if (!type_expr_idx.isvalid()) {
        return semantic::NKType::makeUnknown();
    }
    const auto &node = unit.pool.getNode(type_expr_idx);

    // 1)typeExpr:内建类型
    if (node.type == syntax::NodeType::TypeExpr) {
        switch (node.payload.type_expr.base_type) {
        case syntax::TokenType::KW_INT:
            return semantic::NKType::makeInt();
        case syntax::TokenType::KW_FLOAT:
            return semantic::NKType::makeFloat();
        case syntax::TokenType::KW_BOOL:
            return semantic::NKType::makeBool();
        case syntax::TokenType::KW_STRING:
            return semantic::NKType(semantic::NKBaseType::String, -1);
        default:
            diagnostics.error(diagnostic::events::SemanticCode::GenericError,
                              "Unknown built-in type annotation in predeclare.",
                              diagnostic::makeSourceSpan(unit.source_path, line, column));
            return semantic::NKType::makeUnknown();
        }
    }

    // 2)IdentifierExpre:先匹配内建别名，再看全局符号
    if (node.type == syntax::NodeType::IdentifierExpr) {
        uint32_t name_id = node.payload.identifier.name_id;

        if (name_id == unit.pool.ID_INT) {
            return semantic::NKType::makeInt();
        }
        if (name_id == unit.pool.ID_FLOAT) {
            return semantic::NKType::makeFloat();
        }
        if (name_id == unit.pool.ID_BOOL) {
            return semantic::NKType::makeBool();
        }
        if (name_id == unit.pool.ID_STRING) {
            return semantic::NKType(semantic::NKBaseType::String, -1);
        }
        if (const auto *sym = global_symbols.find(name_id); sym != nullptr) {
            return sym->type; // struct会是nktype::object(global_struct_id)     }
        }

        diagnostics.error(diagnostic::events::SemanticCode::GenericError, "Unknown type name in predeclare.",
                          diagnostic::makeSourceSpan(unit.source_path, line, column));
        return semantic::NKType::makeUnknown();
    }
    diagnostics.error(diagnostic::events::SemanticCode::GenericError, "Invalid type annotation node in predeclare.",
                      diagnostic::makeSourceSpan(unit.source_path, line, column));
    return semantic::NKType::makeUnknown();
};

std::vector<std::string> Driver::collectNkFiles(const std::string &root_dir, const DriverOptions &options) {
    // 目录扫描阶段：只做“找文件”，不做任何编译工作。
    std::vector<std::string> files;
    std::error_code errcode;

    fs::path root(root_dir);
    if (!fs::exists(root, errcode) || !fs::is_directory(root, errcode)) {
        return files;
    }

    auto accept = [&](const fs::directory_entry &entry) {
        if (!entry.is_regular_file()) {
            return false;
        }
        return entry.path().extension().string() == options.file_ext;
    };
    if (options.recursive_scan) {
        // 递归扫描：适合项目模式（子目录按模块组织）。
        for (fs::recursive_directory_iterator file_iterator(root, errcode), end; file_iterator != end && !errcode;
             file_iterator.increment(errcode)) {
            if (errcode) {
                break;
            }
            if (accept(*file_iterator)) {
                files.push_back(file_iterator->path().string());
            }
        }
    } else {
        // 非递归扫描：适合单目录脚本模式。
        for (fs::directory_iterator file_iterator(root, errcode), end; file_iterator != end && !errcode;
             file_iterator.increment(errcode)) {
            if (errcode) {
                break;
            }
            if (accept(*file_iterator)) {
                files.push_back(file_iterator->path().string());
            }
        }
    }

    // 对结果排序，保证每次构建顺序稳定（便于复现与调试）。
    std::sort(files.begin(), files.end());
    return files;
};

std::expected<void, diagnostic::DiagnosticBag> parseIntoCompilationUnit(GlobalCompilationUnit &unit) {
    unit.tokens.clear();
    syntax::Scanner scanner(unit.source, unit.source_path);

    while (true) {
        auto token = scanner.scanToken();
        unit.tokens.push_back(token);

        if (token.type == syntax::TokenType::TOKEN_EOF) {
            break;
        }
    }
    auto scannerDiagnostics = scanner.takeDiagnostics();
    if (!scannerDiagnostics.empty()) {
        return std::unexpected(std::move(scannerDiagnostics));
    }

    unit.pool.source_path = unit.source_path;
    syntax::Parser parser(unit.source, unit.tokens, unit.pool, unit.source_path);
    auto parse_result = parser.parse();

    if (!parse_result.diagnostics.empty()) {
        return std::unexpected(std::move(parse_result.diagnostics));
    }

    unit.root = parse_result.root;
    return {};
}

// 1)读取并解析单格文件
std::expected<GlobalCompilationUnit, diagnostic::DiagnosticBag> Driver::parseOneUnit(
    const std::string &source_path,
    syntax::GlobalInterner &interner) { // 单模块流水线：读文件 -> 扫描 -> 解析 -> 类型检查 -> 编译。
    GlobalCompilationUnit unit(interner);
    unit.source_path = source_path;

    std::ifstream in(source_path, std::ios::binary);
    if (!in.is_open()) {
        return std::unexpected(
            makeDriverError(diagnostic::events::DriverCode::IoError, "Failed to open source file.", source_path));
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    unit.source = buffer.str();

    auto parsed = parseIntoCompilationUnit(unit);
    if (!parsed.has_value()) {
        return std::unexpected(std::move(parsed.error()));
    }
    return unit;
};

// 2)单元及语义检查
std::expected<semantic::TypeCheckResult, diagnostic::DiagnosticBag> Driver::typeCheckUnit(
    GlobalCompilationUnit &unit, GlobalTypeArena &global_arena, GlobalSymbolTable &global_symbols) {
    semantic::TypeChecker checker;
    auto result = checker.check(unit.pool, unit.root, global_symbols, global_arena);
    if (!result.has_value()) {
        return std::unexpected(std::move(result.error()));
    }

    if (unit.pool.node_types.size() != unit.pool.nodes.size()) {
        diagnostic::DiagnosticBag bag;
        bag.error(diagnostic::events::SemanticCode::GenericError, "Type table size mismatch after type check.",
                  diagnostic::makeSourceSpan(unit.source_path));
        return std::unexpected(std::move(bag));
    }

    return result.value();
};
// 3)单元级字节码编译
std::expected<Driver::UnitCompileArtifact, diagnostic::DiagnosticBag> Driver::compileUnitChunk(
    GlobalCompilationUnit &unit, GlobalTypeArena &global_arena, GlobalSymbolTable &global_symbols) {
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
            verify_message << "IR verify failed"
                           << " code=" << static_cast<uint16_t>(issue.error_code) << " func_idx=" << issue.func_idx
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
    artifact.init_chunk =
        makeInitChunkFromLoweredFunctions(lower_result.value().functions, ir_result.value().string_pool);
    artifact.exports = collectExportsFromUnitTopDecls(unit);
    for (const auto &[symbol_name_id, exported_symbol_id] :
         collectExportsFromLoweredFunctions(lower_result.value().functions)) {
        artifact.exports[symbol_name_id] = exported_symbol_id;
    }
    return artifact;
};
// 4)将编译结果组装为linker：：compileModule
linker::CompileModule Driver::buildCompileModule(std::string source_path, UnitCompileArtifact artifact) {
    linker::CompileModule module;
    module.module_name = fs::path(source_path).stem().string();
    module.source_path = std::move(source_path);
    module.init_chunk = std::move(artifact.init_chunk);
    module.exports = std::move(artifact.exports);
    return module;
};
// 单元编译：compile->module（typecheck 在 Pass-3 完成）
std::expected<linker::CompileModule, diagnostic::DiagnosticBag> Driver::compileParsedUnit(
    GlobalCompilationUnit &unit, GlobalTypeArena &global_arena, GlobalSymbolTable &global_symbols) {
    auto artifact_result = compileUnitChunk(unit, global_arena, global_symbols);
    if (!artifact_result.has_value()) {
        return std::unexpected(std::move(artifact_result.error()));
    }
    return buildCompileModule(std::move(unit.source_path), std::move(artifact_result.value()));
};

std::expected<std::vector<linker::CompileModule>, diagnostic::DiagnosticBag> Driver::compileAll(

    const std::vector<std::string> &files) {
    std::vector<linker::CompileModule> modules;
    diagnostic::DiagnosticBag diagnostics;

    // 全项目共享interner，保证name_id跨文件一致
    syntax::GlobalInterner interner;

    GlobalTypeArena global_arena;
    GlobalSymbolTable global_symbols;

    std::vector<GlobalCompilationUnit> units;

    units.reserve(files.size());
    // Pass-1: 先把所有文件 parse 成 unit（不要边 parse 边 compile）
    for (const auto &file : files) {
        auto unit_result = parseOneUnit(file, interner);
        if (!unit_result.has_value()) {
            diagnostics.merge(std::move(unit_result.error()));
            continue;
        }
        units.push_back(std::move(unit_result.value()));
    }

    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }

    // Pass-2: 全项目预声明（注册顶层 func/struct 到 global_symbols/global_arena）
    auto predeclare_result = predeclareAllUnits(units, global_arena, global_symbols);
    if (!predeclare_result.has_value()) {
        diagnostics.merge(std::move(predeclare_result.error()));
    }

    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    // Pass-3: 在共享全局符号环境下做 typecheck
    for (auto &unit : units) {
        auto type_check_result = typeCheckUnit(unit, global_arena, global_symbols);
        if (!type_check_result.has_value()) {
            diagnostics.merge(std::move(type_check_result.error()));
        }
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    // Pass-4: 全部语义通过后再编译成模块
    modules.reserve(units.size());

    for (auto &unit : units) {
        auto module_result = compileParsedUnit(unit, global_arena, global_symbols);
        if (!module_result.has_value()) {
            diagnostics.merge(std::move(module_result.error()));
            continue;
        }
        modules.push_back(std::move(module_result.value()));
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return modules;
};

std::expected<void, diagnostic::DiagnosticBag> predeclareSingleUnit(const GlobalCompilationUnit &unit,
                                                                    GlobalTypeArena &global_arena,
                                                                    GlobalSymbolTable &global_symbols) {
    diagnostic::DiagnosticBag diagnostics;

    if (!unit.root.isvalid()) {
        diagnostics.error(diagnostic::events::SemanticCode::GenericError, "Invalid module root in predeclare.",
                          diagnostic::makeSourceSpan(unit.source_path));
        return std::unexpected(std::move(diagnostics));
    }

    const auto &root = unit.pool.getNode(unit.root);
    if (root.type != syntax::NodeType::ModuleDecl) {
        diagnostics.error(diagnostic::events::SemanticCode::GenericError, "Root node must be ModuleDecl in predeclare.",
                          diagnostic::makeSourceSpan(unit.source_path));
        return std::unexpected(std::move(diagnostics));
    }

    const auto &body_node = unit.pool.getNode(root.payload.module_decl.body);
    auto decls = unit.pool.get_list(body_node.payload.list.elements);

    for (auto decl_idx : decls) {
        if (!decl_idx.isvalid()) {
            continue;
        }
        const auto &decl = unit.pool.getNode(decl_idx);
        uint32_t line = unit.pool.locations[decl_idx.index].line;
        uint32_t column = unit.pool.locations[decl_idx.index].column;

        if (decl.type == syntax::NodeType::StructDecl) {
            uint32_t struct_index = decl.payload.struct_decl.struct_index;
            const auto &struct_data = unit.pool.struct_data[struct_index];
            std::vector<uint32_t> field_name_ids;
            std::vector<semantic::NKType> field_types;
            auto field_name_nodes = unit.pool.get_list(struct_data.names);
            auto field_type_nodes = unit.pool.get_list(struct_data.types);
            field_name_ids.reserve(field_name_nodes.size());
            field_types.reserve(field_type_nodes.size());
            for (auto field_name_idx : field_name_nodes) {
                if (!field_name_idx.isvalid()) {
                    continue;
                }
                field_name_ids.push_back(unit.pool.getNode(field_name_idx).payload.identifier.name_id);
            }
            for (auto field_type_idx : field_type_nodes) {
                field_types.push_back(
                    resolvePredeclareType(unit, field_type_idx, global_symbols, diagnostics, line, column));
            }

            uint32_t global_struct_id = global_arena.internStruct(struct_data.name_id, unit.source_path,
                                                                  std::move(field_name_ids), std::move(field_types));

            GlobalSymbol sym{.name_id = struct_data.name_id,
                             .kind = Kind::Struct,
                             .type = semantic::NKType::makeObject(static_cast<int32_t>(global_struct_id)),
                             .owner_module = unit.source_path};

            if (!global_symbols.insert(std::move(sym))) {
                diagnostics.error(diagnostic::events::SemanticCode::GenericError,
                                  "Duplicate top-level symbol (struct).",
                                  diagnostic::makeSourceSpan(unit.source_path, line, column));
            }
            continue;
        }
        if (decl.type == syntax::NodeType::FunctionDecl) {
            const auto &func_data = unit.pool.function_data[decl.payload.func_decl.function_index];

            std::vector<semantic::NKType> param_types;
            auto params = unit.pool.get_list(func_data.params);
            param_types.reserve(params.size());

            for (auto param_idx : params) {
                const auto &param_node = unit.pool.getNode(param_idx);
                auto type_expr_idx = param_node.payload.var_decl.type_expr;
                param_types.push_back(
                    resolvePredeclareType(unit, type_expr_idx, global_symbols, diagnostics, line, column));
            }
            semantic::NKType ret_type = semantic::NKType::makeUnknown();
            if (func_data.return_type.isvalid()) {
                ret_type =
                    resolvePredeclareType(unit, func_data.return_type, global_symbols, diagnostics, line, column);
            }
            semantic::FunctionSignature sig{param_types, ret_type};
            uint32_t global_sig_id = global_arena.internFuncSig(sig);

            GlobalSymbol sym{
                .name_id = func_data.name_id,
                .kind = Kind::Function,
                .type = semantic::NKType(semantic::NKBaseType::Function, static_cast<int32_t>(global_sig_id)),
                .owner_module = unit.source_path,

            };

            if (!global_symbols.insert(std::move(sym))) {
                diagnostics.error(diagnostic::events::SemanticCode::GenericError,
                                  "Duplicate top-level symbol (function).",
                                  diagnostic::makeSourceSpan(unit.source_path, line, column));
            }
            continue;
        }
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return {};
}

std::expected<void, diagnostic::DiagnosticBag> Driver::predeclareAllUnits(
    const std::vector<GlobalCompilationUnit> &units, GlobalTypeArena &global_arena, GlobalSymbolTable &global_symbols) {
    diagnostic::DiagnosticBag diagnostics;

    for (const auto &unit : units) {
        auto one = predeclareSingleUnit(unit, global_arena, global_symbols);
        if (!one.has_value()) {
            diagnostics.merge(std::move(one.error()));
        }
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return {};
};
std::expected<vm::Value, diagnostic::DiagnosticBag> Driver::runProject(const std::string root_dir,
                                                                       const DriverOptions &options) {
    // 总控流程：
    // A. 收集 .nk 文件
    // B. 编译全部模块
    // C. 交给 linker 产出 LinkedProgram
    // D. 交给 launcher 在 VM 中启动
    auto files = collectNkFiles(root_dir, options);
    if (files.empty()) {
        return std::unexpected(
            makeDriverError(diagnostic::events::DriverCode::NoInput, "No .nk source files found.", root_dir));
    }
    auto compiled = compileAll(files);

    if (!compiled.has_value()) {
        return std::unexpected(compiled.error());
    }

    linker::Linker linker;
    linker::LinkOptions link_options;
    link_options.entry_name = options.entry_name;

    auto linked = linker.link(compiled.value(), link_options);
    if (!linked.has_value()) {
        return std::unexpected(std::move(linked.error()));
    }

    vm::VM vm;
    runtime::Launcher launcher;
    runtime::LaunchOptions launch_options;

    auto launch_result = launcher.launchProgram(vm, linked.value(), launch_options);
    if (!launch_result.has_value()) {
        return std::unexpected(std::move(launch_result.error()));
    }
    return launch_result.value();
};

// 阶段2入口:构建模块语义上下文
std::expected<void, diagnostic::DiagnosticBag> Driver::buildModuleSemanticContext(
    const std::vector<GlobalCompilationUnit> &units, const GlobalSymbolTable &global_symbols,
    semantic::ModuleRegistry &out_regisry, semantic::ModuleExportTable &out_exports,
    std::vector<semantic::UnitVisibleSymbols> &out_visible_per_unit) {};

// 子步骤
std::expected<semantic::ModuleRegistry, diagnostic::DiagnosticBag> Driver::collectModuleRegistry(
    const std::vector<GlobalCompilationUnit> &units) {
    diagnostic::DiagnosticBag diagnostics;
    semantic::ModuleRegistry registry{};

    registry.modules.reserve(units.size());

    for (size_t unit_idx = 0; unit_idx < units.size(); ++unit_idx) {
        const auto &unit = units[unit_idx];
        if (!unit.root.isvalid()) {
            diagnostics.error(diagnostic::events::SemanticCode::GenericError,
                              "Invalid root for module registry collection.",
                              diagnostic::makeSourceSpan(unit.source_path));
            continue;
        }
    }
};

std::expected<semantic::ModuleExportTable, diagnostic::DiagnosticBag> Driver::buildModuleExportTable(
    const std::vector<GlobalCompilationUnit> &units, const semantic::ModuleRegistry &registry,
    const GlobalSymbolTable &global_symbols) {};

std::expected<std::vector<semantic::UnitVisibleSymbols>, diagnostic::DiagnosticBag> Driver::resolveVisibleSymbols(
    const std::vector<GlobalCompilationUnit> &units, const semantic::ModuleRegistry &registry,
    const semantic::ModuleExportTable &export_table, const GlobalSymbolTable &global_symbols) {};
} // namespace niki::driver
