#include "niki/driver/driver.hpp"
#include "niki/l0_core/diagnostic/codes.hpp"
#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/linker/linker.hpp"
#include "niki/l0_core/runtime/launcher.hpp"
#include "niki/l0_core/semantic/global_compilation.hpp"
#include "niki/l0_core/semantic/global_symbol_table.hpp"
#include "niki/l0_core/semantic/global_type_arena.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/semantic/type_checker.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/ast_payloads.hpp"
#include "niki/l0_core/syntax/compiler.hpp"
#include "niki/l0_core/syntax/global_interner.hpp"
#include "niki/l0_core/syntax/parser.hpp"
#include "niki/l0_core/syntax/scanner.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include "niki/l0_core/vm/chunk.hpp"
#include "niki/l0_core/vm/object.hpp"
#include "niki/l0_core/vm/value.hpp"
#include "niki/l0_core/vm/vm.hpp"
#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace niki::driver {

static diagnostic::DiagnosticBag makeDriverError(std::string code, std::string message, std::string file = "") {
    diagnostic::DiagnosticBag bag;
    bag.reportError(diagnostic::DiagnosticStage::Driver, std::move(code), std::move(message),
                    diagnostic::makeSourceSpan(std::move(file)));
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
            diagnostics.reportError(diagnostic::DiagnosticStage::Semantic, diagnostic::codes::semantic::GenericError,
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

        diagnostics.reportError(diagnostic::DiagnosticStage::Semantic, diagnostic::codes::semantic::GenericError,
                                "Unknown type name in predeclare.",
                                diagnostic::makeSourceSpan(unit.source_path, line, column));
        return semantic::NKType::makeUnknown();
    }
    diagnostics.reportError(diagnostic::DiagnosticStage::Semantic, diagnostic::codes::semantic::GenericError,
                            "Invalid type annotation node in predeclare.",
                            diagnostic::makeSourceSpan(unit.source_path, line, column));
    return semantic::NKType::makeUnknown();
};

std::vector<std::string> Driver::collectNkFiles(const std::string &root_dir, const DriverOptions &options) {
    // 目录扫描阶段：只做“找文件”，不做任何编译工作。
    std::vector<std::string> files;
    std::error_code errcode;

    std::filesystem::path root(root_dir);
    if (!std::filesystem::exists(root, errcode) || !std::filesystem::is_directory(root, errcode)) {
        return files;
    }

    auto accept = [&](const std::filesystem::directory_entry &entry) {
        if (!entry.is_regular_file()) {
            return false;
        }
        return entry.path().extension().string() == options.file_ext;
    };
    if (options.recursive_scan) {
        // 递归扫描：适合项目模式（子目录按模块组织）。
        for (std::filesystem::recursive_directory_iterator file_iterator(root, errcode), end;
             file_iterator != end && !errcode; file_iterator.increment(errcode)) {
            if (errcode) {
                break;
            }
            if (accept(*file_iterator)) {
                files.push_back(file_iterator->path().string());
            }
        }
    } else {
        // 非递归扫描：适合单目录脚本模式。
        for (std::filesystem::directory_iterator file_iterator(root, errcode), end; file_iterator != end && !errcode;
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
            makeDriverError(diagnostic::codes::driver::IoError, "Failed to open source file.", source_path));
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
    auto result = checker.check(unit.pool, unit.root, &global_symbols, &global_arena);
    if (!result.has_value()) {
        return std::unexpected(std::move(result.error()));
    }

    if (unit.pool.node_types.size() != unit.pool.nodes.size()) {
        diagnostic::DiagnosticBag bag;
        bag.reportError(diagnostic::DiagnosticStage::Semantic, diagnostic::codes::semantic::GenericError,
                        "Type table size mismatch after type check.", diagnostic::makeSourceSpan(unit.source_path));
        return std::unexpected(std::move(bag));
    }

    return result.value();
};
// 3)单元级字节码编译
std::expected<Chunk, diagnostic::DiagnosticBag> Driver::compileUnitChunk(const GlobalCompilationUnit &unit,
                                                                         GlobalTypeArena &global_arena,
                                                                         GlobalSymbolTable &global_symbols) {
    syntax::Compiler compiler;
    auto result = compiler.compile(unit.pool, unit.root, unit.pool.node_types, Chunk{}, &global_arena, &global_symbols);
    if (!result.has_value()) {
        return std::unexpected(std::move(result.error()));
    }
    return result.value();
};
// 4)将编译结果组装为linker：：compileModule
linker::CompileModule Driver::buildCompileModule(std::string source_path, Chunk chunk) {
    linker::CompileModule module;
    module.module_name = std::filesystem::path(source_path).stem().string();
    module.source_path = std::move(source_path);
    module.init_chunk = std::move(chunk);

    for (const auto &constant : module.init_chunk.constants) {
        if (constant.type != vm::ValueType::Object || constant.as.object == nullptr) {
            continue;
        }
        auto *object = static_cast<vm::Object *>(constant.as.object);
        if (object->type == vm::ObjType::Function) {
            auto *function_object = static_cast<vm::ObjFunction *>(constant.as.object);
            module.exports[function_object->name_id] = function_object->name_id;
        } else if (object->type == vm::ObjType::StructDef) {
            auto *struct_def = static_cast<vm::ObjStructDef *>(constant.as.object);
            module.exports[struct_def->name_id] = struct_def->name_id;
        }
    }
    return module;
};
// 单元编译：compile->module（typecheck 在 Pass-3 完成）
std::expected<linker::CompileModule, diagnostic::DiagnosticBag> Driver::compileParsedUnit(
    GlobalCompilationUnit &unit, GlobalTypeArena &global_arena, GlobalSymbolTable &global_symbols) {
    auto chunk_result = compileUnitChunk(unit, global_arena, global_symbols);
    if (!chunk_result.has_value()) {
        return std::unexpected(std::move(chunk_result.error()));
    }
    return buildCompileModule(std::move(unit.source_path), std::move(chunk_result.value()));
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
        diagnostics.reportError(diagnostic::DiagnosticStage::Semantic, diagnostic::codes::semantic::GenericError,
                                "Invalid module root in predeclare.", diagnostic::makeSourceSpan(unit.source_path));
        return std::unexpected(std::move(diagnostics));
    }

    const auto &root = unit.pool.getNode(unit.root);
    if (root.type != syntax::NodeType::ModuleDecl) {
        diagnostics.reportError(diagnostic::DiagnosticStage::Semantic, diagnostic::codes::semantic::GenericError,
                                "Root node must be ModuleDecl in predeclare.",
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
                diagnostics.reportError(
                    diagnostic::DiagnosticStage::Semantic, diagnostic::codes::semantic::GenericError,
                    "Duplicate top-level symbol (struct).", diagnostic::makeSourceSpan(unit.source_path, line, column));
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
                diagnostics.reportError(diagnostic::DiagnosticStage::Semantic,
                                        diagnostic::codes::semantic::GenericError,
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
            makeDriverError(diagnostic::codes::driver::NoInput, "No .nk source files found.", root_dir));
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
} // namespace niki::driver
