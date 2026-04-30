#include "niki/meta/precompile/precompile_pipeline.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/ast_payloads.hpp"

/** @meta_precompile_predeclare_impl: 预声明阶段实现
 * 该文件负责从顶层声明提取符号并写入全局符号/类型表，形成后续 typecheck 的先验环境。
 * 其核心目标是“声明先行、定义后验”，不承担模块可见性图计算。
 */
namespace niki::meta::precompile {
namespace {

/**
 * @brief 收集模块顶层声明节点列表。
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
    syntax::ASTNodeIndex body_index = unit.root;
    if (root_node.type == syntax::NodeType::ModuleDecl) {
        const syntax::ASTNode &outer_body_node = unit.pool.getNode(root_node.payload.module_decl.body);
        auto outer_decls_span = unit.pool.get_list(outer_body_node.payload.list.elements);
        syntax::ASTNodeIndex primary_module_decl_idx = syntax::ASTNodeIndex::invalid();
        uint32_t module_decl_count = 0;
        for (syntax::ASTNodeIndex candidate : outer_decls_span) {
            if (!candidate.isvalid()) {
                continue;
            }
            const syntax::ASTNode &cand_node = unit.pool.getNode(candidate);
            if (cand_node.type == syntax::NodeType::ModuleDecl) {
                primary_module_decl_idx = candidate;
                module_decl_count++;
            }
        }
        if (module_decl_count == 1 && primary_module_decl_idx.isvalid()) {
            const syntax::ASTNode &primary_module = unit.pool.getNode(primary_module_decl_idx);
            body_index = primary_module.payload.module_decl.body;
        } else {
            body_index = root_node.payload.module_decl.body;
        }
    } else {
        body_index = unit.root;
    }
    const syntax::ASTNode &body_node = unit.pool.getNode(body_index);
    auto span = unit.pool.get_list(body_node.payload.list.elements);
    decls.assign(span.begin(), span.end());
    return decls;
}

/**
 * @brief 将预声明类型表达式解析为 NKType。
 * @param unit 编译单元。
 * @param type_expr_idx 类型表达式节点索引。
 * @param global_symbols 全局符号表。
 * @param diagnostics 诊断输出。
 * @param line 行号。
 * @param column 列号。
 * @return semantic::NKType 解析得到的类型；失败时返回 Unknown 并写诊断。
 */
semantic::NKType resolvePredeclareType(const GlobalCompilationUnit &unit, syntax::ASTNodeIndex type_expr_idx,
                                       const GlobalSymbolTable &global_symbols, diagnostic::DiagnosticBag &diagnostics,
                                       uint32_t line, uint32_t column) {
    if (!type_expr_idx.isvalid()) {
        return semantic::NKType::makeUnknown();
    }
    const auto &node = unit.pool.getNode(type_expr_idx);
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
            diagnostics.error(diagnostic::events::SemanticCode::GenericError, "Unknown built-in type annotation in predeclare.",
                              diagnostic::makeSourceSpan(unit.source_path, line, column));
            return semantic::NKType::makeUnknown();
        }
    }
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
            return sym->type;
        }
        diagnostics.error(diagnostic::events::SemanticCode::GenericError, "Unknown type name in predeclare.",
                          diagnostic::makeSourceSpan(unit.source_path, line, column));
        return semantic::NKType::makeUnknown();
    }
    diagnostics.error(diagnostic::events::SemanticCode::GenericError, "Invalid type annotation node in predeclare.",
                      diagnostic::makeSourceSpan(unit.source_path, line, column));
    return semantic::NKType::makeUnknown();
}

} // namespace

/**
 * @brief 对单编译单元执行顶层符号预声明。
 * @param unit 编译单元。
 * @param global_arena 全局类型 arena。
 * @param global_symbols 全局符号表。
 * @return std::expected<void, diagnostic::DiagnosticBag> 成功返回空，失败返回聚合诊断。
 */
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
    auto decls = collectTopLevelDecls(unit);
    auto predeclare_typealias_decl = [&](const syntax::ASTNode &typealias_node, uint32_t at_line, uint32_t at_column,
                                         const char *duplicate_msg) {
        const auto &type_alias = typealias_node.payload.type_alias;
        semantic::NKType alias_type =
            resolvePredeclareType(unit, type_alias.type_expr, global_symbols, diagnostics, at_line, at_column);
        GlobalSymbol sym{
            .name_id = type_alias.name_id,
            .kind = Kind::TypeAlias,
            .type = alias_type,
            .owner_module = unit.source_path,
        };
        if (!global_symbols.insert(std::move(sym))) {
            diagnostics.error(diagnostic::events::SemanticCode::GenericError, duplicate_msg,
                              diagnostic::makeSourceSpan(unit.source_path, at_line, at_column));
        }
    };
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
                field_types.push_back(resolvePredeclareType(unit, field_type_idx, global_symbols, diagnostics, line, column));
            }
            uint32_t global_struct_id =
                global_arena.internStruct(struct_data.name_id, unit.source_path, std::move(field_name_ids), std::move(field_types));
            GlobalSymbol sym{.name_id = struct_data.name_id,
                             .kind = Kind::Struct,
                             .type = semantic::NKType::makeObject(static_cast<int32_t>(global_struct_id)),
                             .owner_module = unit.source_path};
            if (!global_symbols.insert(std::move(sym))) {
                diagnostics.error(diagnostic::events::SemanticCode::GenericError, "Duplicate top-level symbol (struct).",
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
                param_types.push_back(
                    resolvePredeclareType(unit, param_node.payload.var_decl.type_expr, global_symbols, diagnostics, line, column));
            }
            semantic::NKType ret_type = semantic::NKType::makeUnknown();
            if (func_data.return_type.isvalid()) {
                ret_type = resolvePredeclareType(unit, func_data.return_type, global_symbols, diagnostics, line, column);
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
                diagnostics.error(diagnostic::events::SemanticCode::GenericError, "Duplicate top-level symbol (function).",
                                  diagnostic::makeSourceSpan(unit.source_path, line, column));
            }
            continue;
        }
        if (decl.type == syntax::NodeType::ExportDecl) {
            const auto &export_decl = unit.pool.export_decl_data[decl.payload.export_decl.export_decl_index];
            if (export_decl.has_wrapped_decl && export_decl.wrapped_decl.isvalid()) {
                const auto &wrapped_node = unit.pool.getNode(export_decl.wrapped_decl);
                if (wrapped_node.type == syntax::NodeType::FunctionDecl) {
                    const auto &func_data = unit.pool.function_data[wrapped_node.payload.func_decl.function_index];
                    std::vector<semantic::NKType> param_types;
                    auto params = unit.pool.get_list(func_data.params);
                    param_types.reserve(params.size());
                    for (auto param_idx : params) {
                        const auto &param_node = unit.pool.getNode(param_idx);
                        param_types.push_back(resolvePredeclareType(unit, param_node.payload.var_decl.type_expr, global_symbols,
                                                                    diagnostics, line, column));
                    }
                    semantic::NKType ret_type = semantic::NKType::makeUnknown();
                    if (func_data.return_type.isvalid()) {
                        ret_type = resolvePredeclareType(unit, func_data.return_type, global_symbols, diagnostics, line, column);
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
                                          "Duplicate top-level symbol (export wrapped function).",
                                          diagnostic::makeSourceSpan(unit.source_path, line, column));
                    }
                } else if (wrapped_node.type == syntax::NodeType::StructDecl) {
                    uint32_t struct_index = wrapped_node.payload.struct_decl.struct_index;
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
                        field_types.push_back(resolvePredeclareType(unit, field_type_idx, global_symbols, diagnostics, line, column));
                    }
                    uint32_t global_struct_id =
                        global_arena.internStruct(struct_data.name_id, unit.source_path, std::move(field_name_ids), std::move(field_types));
                    GlobalSymbol sym{.name_id = struct_data.name_id,
                                     .kind = Kind::Struct,
                                     .type = semantic::NKType::makeObject(static_cast<int32_t>(global_struct_id)),
                                     .owner_module = unit.source_path};
                    if (!global_symbols.insert(std::move(sym))) {
                        diagnostics.error(diagnostic::events::SemanticCode::GenericError,
                                          "Duplicate top-level symbol (export wrapped struct).",
                                          diagnostic::makeSourceSpan(unit.source_path, line, column));
                    }
                } else if (wrapped_node.type == syntax::NodeType::TypeAliasDecl) {
                    predeclare_typealias_decl(wrapped_node, line, column, "Duplicate top-level symbol (export wrapped typealias).");
                }
            }
            continue;
        }
        if (decl.type == syntax::NodeType::TypeAliasDecl) {
            predeclare_typealias_decl(decl, line, column, "Duplicate top-level symbol (typealias).");
            continue;
        }
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return {};
}

} // namespace niki::meta::precompile
