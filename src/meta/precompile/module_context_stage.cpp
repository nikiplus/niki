#include "niki/l0_core/semantic/module_namespace.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/ast_payloads.hpp"
#include "niki/meta/precompile/precompile_pipeline.hpp"
#include <filesystem>
#include <limits>
#include <unordered_map>

/** @meta_precompile_module_context_impl: 模块语义上下文阶段实现
 * 该文件把多 unit 的 import/export/同模块可见性汇总为统一语义上下文，
 * 为项目级 typecheck 提供显式可见符号表输入。
 */
namespace niki::meta::precompile {
namespace {

/**
 * @brief 构建模块注册表与 import 关系。
 * @param units 全部编译单元。
 * @return std::expected<semantic::ModuleRegistry, diagnostic::DiagnosticBag> 成功返回 registry，失败返回诊断。
 */
std::expected<semantic::ModuleRegistry, diagnostic::DiagnosticBag> collectModuleRegistry(
    const std::vector<CompilationUnit> &units) {
    diagnostic::DiagnosticBag diagnostics;
    semantic::ModuleRegistry registry{};
    std::unordered_map<uint32_t, uint32_t> module_name_id_to_module_id;
    registry.modules.reserve(units.size());

    auto register_module_name_key = [&](uint32_t name_id, ModuleId mid, const std::string &path) {
        auto result = module_name_id_to_module_id.emplace(name_id, mid);
        if (!result.second && result.first->second != mid) {
            diagnostics.error(diagnostic::events::SemanticCode::GenericError,
                              "Duplicate module file stem or logical module name conflicts with another unit.",
                              diagnostic::makeSourceSpan(path));
        }
    };

    for (size_t unit_idx = 0; unit_idx < units.size(); ++unit_idx) {
        const auto &unit = units[unit_idx];
        if (!unit.root.isvalid()) {
            diagnostics.error(diagnostic::events::SemanticCode::GenericError,
                              "Invalid root for module registry collection.",
                              diagnostic::makeSourceSpan(unit.source_path));
            continue;
        }
        semantic::ModuleMeta meta{};
        meta.module_id = unit.module_id;
        meta.source_path = unit.source_path;
        meta.unit_index = unit_idx;
        registry.module_id_to_meta_index.emplace(meta.module_id, registry.modules.size());
        registry.modules.push_back(std::move(meta));

        if (unit.pool.interner != nullptr) {
            const auto file_stem = std::filesystem::path(unit.source_path).stem().string();
            uint32_t stem_name_id = unit.pool.interner->intern(file_stem);
            register_module_name_key(stem_name_id, unit.module_id, unit.source_path);
        }
        const auto &root_node = unit.pool.getNode(unit.root);
        if (root_node.type == syntax::NodeType::ModuleDecl &&
            root_node.payload.module_decl.name_id != syntax::kSyntheticModuleRootNameId) {
            register_module_name_key(root_node.payload.module_decl.name_id, unit.module_id, unit.source_path);
        }
    }
    for (size_t unit_idx = 0; unit_idx < units.size(); ++unit_idx) {
        auto &module_meta = registry.modules[unit_idx];
        const auto &unit = units[unit_idx];
        forEachModuleScopedDecl(unit, [&](syntax::ASTNodeIndex decl_idx) {
            if (!decl_idx.isvalid()) {
                return;
            }
            const auto &decl_node = unit.pool.getNode(decl_idx);
            if (decl_node.type != syntax::NodeType::ImportDecl) {
                return;
            }
            uint32_t line = 0;
            uint32_t column = 0;
            if (decl_idx.index < unit.pool.locations.size()) {
                line = unit.pool.locations[decl_idx.index].line;
                column = unit.pool.locations[decl_idx.index].column;
            }
            const auto &import_decl = unit.pool.import_decl_data[decl_node.payload.import_decl.import_decl_index];
            auto imported_module_iter = module_name_id_to_module_id.find(import_decl.module_name_id);
            if (imported_module_iter == module_name_id_to_module_id.end()) {
                diagnostics.error(diagnostic::events::SemanticCode::GenericError, "Imported module not found.",
                                  diagnostic::makeSourceSpan(unit.source_path, line, column));
                return;
            }
            const uint32_t from_module_id = imported_module_iter->second;
            if (import_decl.import_module_only) {
                return;
            }
            for (uint32_t offset = 0; offset < import_decl.item_count; ++offset) {
                const auto &item = unit.pool.import_items[import_decl.first_item_index + offset];
                module_meta.imports.push_back(semantic::ImportBinding{
                    .from_module_id = from_module_id,
                    .imported_name_id = item.imported_name_id,
                    .local_name_id = item.local_name_id,
                    .import_line = line,
                    .import_column = column,
                });
            }
        });
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return registry;
}

/**
 * @brief 构建模块导出表（基于 ModuleNamespace）。
 * @param units 全部编译单元。
 * @param registry 模块注册表。
 * @param module_namespace 模块命名空间。
 * @return std::expected<semantic::ModuleExportTable, diagnostic::DiagnosticBag> 成功返回导出表，失败返回诊断。
 */
std::expected<semantic::ModuleExportTable, diagnostic::DiagnosticBag> buildModuleExportTable(
    const std::vector<CompilationUnit> &units, const semantic::ModuleRegistry &registry,
    const ModuleNamespace &module_namespace) {
    diagnostic::DiagnosticBag diagnostics;
    semantic::ModuleExportTable export_table{};
    for (size_t unit_idx = 0; unit_idx < units.size(); ++unit_idx) {
        const auto &unit = units[unit_idx];
        const auto &module_meta = registry.modules[unit_idx];
        auto &module_exports = export_table.table[module_meta.module_id];
        forEachModuleScopedDecl(unit, [&](syntax::ASTNodeIndex decl_idx) {
            if (!decl_idx.isvalid()) {
                return;
            }
            const auto &decl_node = unit.pool.getNode(decl_idx);
            if (decl_node.type != syntax::NodeType::ExportDecl) {
                return;
            }
            const auto &export_decl = unit.pool.export_decl_data[decl_node.payload.export_decl.export_decl_index];
            if (export_decl.has_wrapped_decl && export_decl.wrapped_decl.isvalid()) {
                const auto &wrapped_node = unit.pool.getNode(export_decl.wrapped_decl);
                uint32_t local_name_id = std::numeric_limits<uint32_t>::max();
                if (wrapped_node.type == syntax::NodeType::FunctionDecl) {
                    const auto &func_data = unit.pool.function_data[wrapped_node.payload.func_decl.function_index];
                    local_name_id = func_data.name_id;
                } else if (wrapped_node.type == syntax::NodeType::StructDecl) {
                    const auto &struct_data = unit.pool.struct_data[wrapped_node.payload.struct_decl.struct_index];
                    local_name_id = struct_data.name_id;
                } else if (wrapped_node.type == syntax::NodeType::TypeAliasDecl) {
                    local_name_id = wrapped_node.payload.type_alias.name_id;
                }
                if (local_name_id != std::numeric_limits<uint32_t>::max()) {
                    const auto *symbol = module_namespace.find(module_meta.module_id, local_name_id);
                    if (symbol != nullptr) {
                        module_exports.emplace(local_name_id, semantic::SymbolRef{
                                                                  .owner_module_id = module_meta.module_id,
                                                                  .name_id = local_name_id,
                                                                  .kind = symbol->kind,
                                                                  .type = symbol->type,
                                                              });
                    } else {
                        diagnostics.error(diagnostic::events::SemanticCode::GenericError,
                                          "Exported wrapped symbol missing from module namespace.",
                                          diagnostic::makeSourceSpan(unit.source_path));
                    }
                }
                return;
            }
            if (export_decl.item_count == 0) {
                return;
            }
            for (uint32_t offset = 0; offset < export_decl.item_count; ++offset) {
                const auto &item = unit.pool.export_items[export_decl.first_item_index + offset];
                const auto *symbol = module_namespace.find(module_meta.module_id, item.local_name_id);
                if (symbol == nullptr) {
                    diagnostics.error(diagnostic::events::SemanticCode::GenericError,
                                      "Exported symbol missing from module namespace.",
                                      diagnostic::makeSourceSpan(unit.source_path));
                    continue;
                }
                module_exports.emplace(item.exported_name_id, semantic::SymbolRef{
                                                                  .owner_module_id = module_meta.module_id,
                                                                  .name_id = item.exported_name_id,
                                                                  .kind = symbol->kind,
                                                                  .type = symbol->type,
                                                              });
            }
        });
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return export_table;
}

/**
 * @brief 解析每个编译单元的可见符号表（基于 ModuleNamespace，O(1) 查询同模块符号）。
 * @param units 全部编译单元。
 * @param registry 模块注册表。
 * @param export_table 模块导出表。
 * @param module_namespace 模块命名空间。
 * @return std::expected<std::vector<semantic::UnitVisibleSymbols>, diagnostic::DiagnosticBag>
 * 成功返回可见性表，失败返回诊断。
 */
std::expected<std::vector<semantic::UnitVisibleSymbols>, diagnostic::DiagnosticBag> resolveVisibleSymbols(
    const std::vector<CompilationUnit> &units, const semantic::ModuleRegistry &registry,
    const semantic::ModuleExportTable &export_table, const ModuleNamespace &module_namespace) {
    diagnostic::DiagnosticBag diagnostics;
    std::vector<semantic::UnitVisibleSymbols> visible_per_unit(units.size());
    for (size_t unit_idx = 0; unit_idx < units.size(); ++unit_idx) {
        const auto &unit = units[unit_idx];
        const auto &module_meta = registry.modules[unit_idx];
        auto &visible = visible_per_unit[unit_idx].tables;
        // 使用 ModuleNamespace::findModuleSymbols() O(1) 获取本模块所有符号
        auto module_symbols = module_namespace.findModuleSymbols(module_meta.module_id);
        for (const auto *sym : module_symbols) {
            visible.insert_or_assign(sym->name_id, semantic::SymbolRef{
                                                       .owner_module_id = module_meta.module_id,
                                                       .name_id = sym->name_id,
                                                       .kind = sym->kind,
                                                       .type = sym->type,
                                                   });
        }
        for (const auto &binding : module_meta.imports) {
            auto from_module_iter = export_table.table.find(binding.from_module_id);
            if (from_module_iter == export_table.table.end()) {
                diagnostics.error(diagnostic::events::SemanticCode::GenericError,
                                  "Internal error: imported module missing from export table.",
                                  diagnostic::makeSourceSpan(unit.source_path, binding.import_line, binding.import_column));
                continue;
            }
            auto symbol_iter = from_module_iter->second.find(binding.imported_name_id);
            if (symbol_iter == from_module_iter->second.end()) {
                diagnostics.error(diagnostic::events::SemanticCode::GenericError,
                                  "Imported name is not exported by the target module (or target exports nothing).",
                                  diagnostic::makeSourceSpan(unit.source_path, binding.import_line, binding.import_column));
                continue;
            }
            auto imported_symbol = symbol_iter->second;
            imported_symbol.name_id = binding.local_name_id;
            visible.insert_or_assign(binding.local_name_id, imported_symbol);
        }
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return visible_per_unit;
}

} // namespace

/**
 * @brief 构建项目级模块语义上下文（基于 ModuleNamespace）。
 * @param units 全部编译单元。
 * @param module_namespace 模块命名空间。
 * @return std::expected<ModuleSemanticContext, diagnostic::DiagnosticBag> 成功返回上下文，失败返回诊断。
 */
std::expected<ModuleSemanticContext, diagnostic::DiagnosticBag> buildModuleSemanticContext(
    const std::vector<CompilationUnit> &units, const ModuleNamespace &module_namespace) {
    auto registry = collectModuleRegistry(units);
    if (!registry.has_value()) {
        return std::unexpected(std::move(registry.error()));
    }
    auto export_table = buildModuleExportTable(units, registry.value(), module_namespace);
    if (!export_table.has_value()) {
        return std::unexpected(std::move(export_table.error()));
    }
    auto visible_per_unit = resolveVisibleSymbols(units, registry.value(), export_table.value(), module_namespace);
    if (!visible_per_unit.has_value()) {
        return std::unexpected(std::move(visible_per_unit.error()));
    }
    ModuleSemanticContext context;
    context.registry = std::move(registry.value());
    context.exports = std::move(export_table.value());
    context.visible_per_unit = std::move(visible_per_unit.value());
    return context;
}

} // namespace niki::meta::precompile
