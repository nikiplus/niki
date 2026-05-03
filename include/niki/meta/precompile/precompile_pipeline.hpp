#pragma once

#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/semantic/global_compilation.hpp"
#include "niki/l0_core/semantic/global_symbol_table.hpp"
#include "niki/l0_core/semantic/global_type_arena.hpp"
#include "niki/l0_core/semantic/module_semantic.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include <expected>
#include <vector>

/** @meta_precompile_api: 预编译阶段统一入口
 * 该头文件定义 precompile 层能力边界：解析、预声明、模块语义上下文构建。
 * 其目标是把语义前置准备从 orchestrator 中剥离，形成可复用阶段 API。
 */
namespace niki::meta::precompile {

/// @brief 收集模块顶层声明节点列表（自动拆包合成 outer ModuleDecl，提取显式 module body）。
std::vector<syntax::ASTNodeIndex> collectTopLevelDecls(const GlobalCompilationUnit &unit);

struct ModuleSemanticContext {
    semantic::ModuleRegistry registry;
    semantic::ModuleExportTable exports;
    std::vector<semantic::UnitVisibleSymbols> visible_per_unit;
};

/// @brief 对单编译单元执行 scan+parse，并填充 tokens/root。
std::expected<void, diagnostic::DiagnosticBag> parseIntoCompilationUnit(GlobalCompilationUnit &unit);
/// @brief 对单编译单元执行顶层符号预声明并写入全局表。
std::expected<void, diagnostic::DiagnosticBag> predeclareSingleUnit(const GlobalCompilationUnit &unit,
                                                                    GlobalTypeArena &global_arena,
                                                                    GlobalSymbolTable &global_symbols);
/// @brief 构建项目级模块语义上下文（registry/export/visible symbols）。
std::expected<ModuleSemanticContext, diagnostic::DiagnosticBag>
buildModuleSemanticContext(const std::vector<GlobalCompilationUnit> &units, const GlobalSymbolTable &global_symbols);

} // namespace niki::meta::precompile
