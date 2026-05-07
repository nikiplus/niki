#pragma once

#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/semantic/compilation_unit.hpp"
#include "niki/l0_core/semantic/type_arena.hpp"
#include "niki/l0_core/semantic/module_namespace.hpp"
#include "niki/l0_core/semantic/module_semantic.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include <expected>
#include <utility>
#include <vector>

/** @meta_precompile_api: 预编译阶段统一入口
 * 该头文件定义 precompile 层能力边界：解析、预声明、模块语义上下文构建。
 * 其目标是把语义前置准备从 orchestrator 中剥离，形成可复用阶段 API。
 */
namespace niki::meta::precompile {

/// @brief 收集模块顶层声明节点列表（自动拆包合成 outer ModuleDecl，提取显式 module body）。
std::vector<syntax::ASTNodeIndex> collectTopLevelDecls(const CompilationUnit &unit);

namespace detail {

template <typename F>
void walkModuleScopedDeclsRecursive(const CompilationUnit &unit, syntax::ASTNodeIndex body_idx, F &&fn) {
    if (!body_idx.isvalid()) {
        return;
    }
    const auto &body_node = unit.pool.getNode(body_idx);
    auto span = unit.pool.get_list(body_node.payload.list.elements);
    for (syntax::ASTNodeIndex decl_idx : span) {
        if (!decl_idx.isvalid()) {
            continue;
        }
        const auto &decl_node = unit.pool.getNode(decl_idx);
        if (decl_node.type == syntax::NodeType::ModuleDecl) {
            walkModuleScopedDeclsRecursive(unit, decl_node.payload.module_decl.body, std::forward<F>(fn));
        } else {
            fn(decl_idx);
        }
    }
}

} // namespace detail

/// @brief 深度优先遍历根模块体内所有「叶子」声明（跳过嵌套 module 节点，进入其 body）。
template <typename F>
void forEachModuleScopedDecl(const CompilationUnit &unit, F &&fn) {
    if (!unit.root.isvalid()) {
        return;
    }
    const auto &root_node = unit.pool.getNode(unit.root);
    if (root_node.type != syntax::NodeType::ModuleDecl && root_node.type != syntax::NodeType::ProgramRoot) {
        return;
    }
    syntax::ASTNodeIndex body_index = unit.root;
    if (root_node.type == syntax::NodeType::ModuleDecl) {
        body_index = root_node.payload.module_decl.body;
    }
    detail::walkModuleScopedDeclsRecursive(unit, body_index, std::forward<F>(fn));
}

struct ModuleSemanticContext {
    semantic::ModuleRegistry registry;
    semantic::ModuleExportTable exports;
    std::vector<semantic::UnitVisibleSymbols> visible_per_unit;
};

/// @brief 对单编译单元执行 scan+parse，并填充 tokens/root。
std::expected<void, diagnostic::DiagnosticBag> parseIntoCompilationUnit(CompilationUnit &unit);
/// @brief 对单编译单元执行顶层符号预声明并写入 ModuleNamespace。
std::expected<void, diagnostic::DiagnosticBag> predeclareSingleUnit(const CompilationUnit &unit,
                                                                    TypeArena &global_arena,
                                                                    ModuleNamespace &module_namespace);
/// @brief 构建项目级模块语义上下文（registry/export/visible symbols）。
std::expected<ModuleSemanticContext, diagnostic::DiagnosticBag>
buildModuleSemanticContext(const std::vector<CompilationUnit> &units, const ModuleNamespace &module_namespace);

} // namespace niki::meta::precompile
