#include "niki/meta/precompile/precompile_pipeline.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/ast_payloads.hpp"
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
    const std::vector<GlobalCompilationUnit> &units) {
    diagnostic::DiagnosticBag diagnostics;
    semantic::ModuleRegistry registry{};
    std::unordered_map<uint32_t, uint32_t> module_name_id_to_module_id;
    registry.modules.reserve(units.size());
    for (size_t unit_idx = 0; unit_idx < units.size(); ++unit_idx) {
        const auto &unit = units[unit_idx];
        if (!unit.root.isvalid()) {
            diagnostics.error(diagnostic::events::SemanticCode::GenericError, "Invalid root for module registry collection.",
                              diagnostic::makeSourceSpan(unit.source_path));
            continue;
        }
        semantic::ModuleMeta meta{};
        meta.module_id = static_cast<uint32_t>(unit_idx);
        meta.source_path = unit.source_path;
        meta.unit_index = unit_idx;
        registry.module_id_to_meta_index.emplace(meta.module_id, registry.modules.size());
        registry.modules.push_back(std::move(meta));
        const auto file_stem = std::filesystem::path(unit.source_path).stem().string();
        if (unit.pool.interner != nullptr) {
            auto module_name_id = unit.pool.interner->find(file_stem);
            if (module_name_id.has_value()) {
                module_name_id_to_module_id[*module_name_id] = static_cast<uint32_t>(unit_idx);
            }
        }
    }
    for (size_t unit_idx = 0; unit_idx < units.size(); ++unit_idx) {
        auto &module_meta = registry.modules[unit_idx];
        const auto &unit = units[unit_idx];
        for (const auto decl_idx : collectTopLevelDecls(unit)) {
            if (!decl_idx.isvalid()) {
                continue;
            }
            const auto &decl_node = unit.pool.getNode(decl_idx);
            if (decl_node.type != syntax::NodeType::ImportDecl) {
                continue;
            }
            const auto &import_decl = unit.pool.import_decl_data[decl_node.payload.import_decl.import_decl_index];
            auto imported_module_iter = module_name_id_to_module_id.find(import_decl.module_name_id);
            if (imported_module_iter == module_name_id_to_module_id.end()) {
                diagnostics.error(diagnostic::events::SemanticCode::GenericError, "Imported module not found.",
                                  diagnostic::makeSourceSpan(unit.source_path));
                continue;
            }
            const uint32_t from_module_id = imported_module_iter->second;
            if (import_decl.import_module_only) {
                continue;
            }
            for (uint32_t offset = 0; offset < import_decl.item_count; ++offset) {
                const auto &item = unit.pool.import_items[import_decl.first_item_index + offset];
                module_meta.imports.push_back(semantic::ImportBinding{
                    .from_module_id = from_module_id,
                    .imported_name_id = item.imported_name_id,
                    .local_name_id = item.local_name_id,
                });
            }
        }
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return registry;
}

/**
 * @brief 构建模块导出表。
 * @param units 全部编译单元。
 * @param registry 模块注册表。
 * @param global_symbols 全局符号表。
 * @return std::expected<semantic::ModuleExportTable, diagnostic::DiagnosticBag> 成功返回导出表，失败返回诊断。
 */
std::expected<semantic::ModuleExportTable, diagnostic::DiagnosticBag> buildModuleExportTable(
    const std::vector<GlobalCompilationUnit> &units, const semantic::ModuleRegistry &registry,
    const GlobalSymbolTable &global_symbols) {
    diagnostic::DiagnosticBag diagnostics;
    semantic::ModuleExportTable export_table{};
    for (size_t unit_idx = 0; unit_idx < units.size(); ++unit_idx) {
        const auto &unit = units[unit_idx];
        const auto &module_meta = registry.modules[unit_idx];
        auto &module_exports = export_table.table[module_meta.module_id];
        for (const auto decl_idx : collectTopLevelDecls(unit)) {
            if (!decl_idx.isvalid()) {
                continue;
            }
            const auto &decl_node = unit.pool.getNode(decl_idx);
            if (decl_node.type != syntax::NodeType::ExportDecl) {
                continue;
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
                    const auto *symbol = global_symbols.find(local_name_id);
                    if (symbol != nullptr) {
                        module_exports.emplace(local_name_id, semantic::SymbolRef{
                                                                 .owner_module_id = module_meta.module_id,
                                                                 .name_id = local_name_id,
                                                                 .kind = symbol->kind,
                                                                 .type = symbol->type,
                                                             });
                    } else {
                        diagnostics.error(diagnostic::events::SemanticCode::GenericError,
                                          "Exported wrapped symbol missing from global symbol table.",
                                          diagnostic::makeSourceSpan(unit.source_path));
                    }
                }
                continue;
            }
            if (export_decl.item_count == 0) {
                continue;
            }
            for (uint32_t offset = 0; offset < export_decl.item_count; ++offset) {
                const auto &item = unit.pool.export_items[export_decl.first_item_index + offset];
                const auto *symbol = global_symbols.find(item.local_name_id);
                if (symbol == nullptr) {
                    diagnostics.error(diagnostic::events::SemanticCode::GenericError, "Exported symbol missing from global symbol table.",
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
        }
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return export_table;
}

/**
 * @brief 解析每个编译单元的可见符号表（同模块 + 显式导入）。
 * @param units 全部编译单元。
 * @param registry 模块注册表。
 * @param export_table 模块导出表。
 * @param global_symbols 全局符号表。
 * @return std::expected<std::vector<semantic::UnitVisibleSymbols>, diagnostic::DiagnosticBag> 成功返回可见性表，失败返回诊断。
 */
std::expected<std::vector<semantic::UnitVisibleSymbols>, diagnostic::DiagnosticBag> resolveVisibleSymbols(
    const std::vector<GlobalCompilationUnit> &units, const semantic::ModuleRegistry &registry,
    const semantic::ModuleExportTable &export_table, const GlobalSymbolTable &global_symbols) {
    diagnostic::DiagnosticBag diagnostics;
    std::vector<semantic::UnitVisibleSymbols> visible_per_unit(units.size());
    for (size_t unit_idx = 0; unit_idx < units.size(); ++unit_idx) {
        const auto &unit = units[unit_idx];
        const auto &module_meta = registry.modules[unit_idx];
        auto &visible = visible_per_unit[unit_idx].tables;
        for (const auto &[name_id, symbol] : global_symbols.symbol_table) {
            if (symbol.owner_module != unit.source_path) {
                continue;
            }
            visible.insert_or_assign(name_id, semantic::SymbolRef{
                                                  .owner_module_id = module_meta.module_id,
                                                  .name_id = name_id,
                                                  .kind = symbol.kind,
                                                  .type = symbol.type,
                                              });
        }
        for (const auto &binding : module_meta.imports) {
            auto from_module_iter = export_table.table.find(binding.from_module_id);
            if (from_module_iter == export_table.table.end()) {
                continue;
            }
            auto symbol_iter = from_module_iter->second.find(binding.imported_name_id);
            if (symbol_iter == from_module_iter->second.end()) {
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
 * @brief 构建项目级模块语义上下文。
 * @param units 全部编译单元。
 * @param global_symbols 全局符号表。
 * @return std::expected<ModuleSemanticContext, diagnostic::DiagnosticBag> 成功返回上下文，失败返回诊断。
 */
std::expected<ModuleSemanticContext, diagnostic::DiagnosticBag>
buildModuleSemanticContext(const std::vector<GlobalCompilationUnit> &units, const GlobalSymbolTable &global_symbols) {
    auto registry = collectModuleRegistry(units);
    if (!registry.has_value()) {
        return std::unexpected(std::move(registry.error()));
    }
    auto export_table = buildModuleExportTable(units, registry.value(), global_symbols);
    if (!export_table.has_value()) {
        return std::unexpected(std::move(export_table.error()));
    }
    auto visible_per_unit = resolveVisibleSymbols(units, registry.value(), export_table.value(), global_symbols);
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
