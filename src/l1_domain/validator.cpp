#include "niki/l1_domain/validator.hpp"
#include "niki/l0_core/ir/extensions.hpp"
#include <unordered_set>

namespace niki::l1_domain {
void appendDomainIRChecks(const ir::ModuleIR &module_ir, ir::VerifyReport &report) {
    for (uint32_t kits_index = 0; kits_index < module_ir.kits.size(); ++kits_index) {
        const ir::KitsRecord &kits_record = module_ir.kits[kits_index];
        if (kits_record.first_item + kits_record.item_count > module_ir.kits_items.size()) {
            report.add(ir::VerifyErrorCode::KitsItemSpanOutOfRange, "kits item span out of range", kits_index);
            continue;
        }
        if (kits_record.kits_sid >= module_ir.string_pool.size()) {
            report.add(ir::VerifyErrorCode::KitsNameRefOutOfRange, "kits name sid out of range", kits_index);
        }
        if (kits_record.owner_mod_sid >= module_ir.string_pool.size()) {
            report.add(ir::VerifyErrorCode::KitsOwnerModuleRefOutOfRange, "kits owner module sid out of range", kits_index);
        }

        std::unordered_set<uint32_t> alias_sids;
        alias_sids.reserve(kits_record.item_count);
        for (uint32_t dedup_index = 0; dedup_index < kits_record.item_count; ++dedup_index) {
            const ir::KitsItemRecord &dedup_item = module_ir.kits_items[kits_record.first_item + dedup_index];
            if (!alias_sids.insert(dedup_item.alias_sid).second) {
                report.add(ir::VerifyErrorCode::KitsDuplicateAliasInWindow, "duplicate kits alias in same window",
                           kits_index, dedup_index);
            }
        }

        for (uint32_t rel_item_index = 0; rel_item_index < kits_record.item_count; ++rel_item_index) {
            const ir::KitsItemRecord &item = module_ir.kits_items[kits_record.first_item + rel_item_index];
            if (item.alias_sid >= module_ir.string_pool.size()) {
                report.add(ir::VerifyErrorCode::KitsAliasRefOutOfRange, "kits alias sid out of range", kits_index,
                           rel_item_index);
            }
            if (item.component_sid >= module_ir.string_pool.size()) {
                report.add(ir::VerifyErrorCode::KitsComponentRefOutOfRange, "kits component sid out of range", kits_index,
                           rel_item_index);
            }
        }
    }

    for (uint32_t component_index = 0; component_index < module_ir.components.size(); ++component_index) {
        const ir::ComponentRecord &component_record = module_ir.components[component_index];
        if (component_record.component_sid >= module_ir.string_pool.size()) {
            report.add(ir::VerifyErrorCode::ComponentNameRefOutOfRange, "component name sid out of range",
                       component_index);
        }
        if (component_record.owner_mod_sid >= module_ir.string_pool.size()) {
            report.add(ir::VerifyErrorCode::ComponentOwnerModuleRefOutOfRange, "component owner module sid out of range",
                       component_index);
        }
        if (component_record.is_struct_promotion &&
            component_record.source_struct_sid >= module_ir.string_pool.size()) {
            report.add(ir::VerifyErrorCode::ComponentSourceStructRefOutOfRange,
                       "component source struct sid out of range for promotion", component_index);
        }
    }

    std::unordered_set<uint32_t> exported_symbol_name_sids;
    for (uint32_t symbol_index = 0; symbol_index < module_ir.syms.size(); ++symbol_index) {
        const ir::SymRecord &symbol_record = module_ir.syms[symbol_index];
        if (symbol_record.sym_id != symbol_index) {
            report.add(ir::VerifyErrorCode::SymbolIdMismatch, "symbol id mismatch", symbol_index);
        }
        if (symbol_record.sym_name_sid >= module_ir.string_pool.size()) {
            report.add(ir::VerifyErrorCode::SymbolNameRefOutOfRange, "symbol name sid out of range", symbol_index);
        }
        if (symbol_record.owner_mod_sid >= module_ir.string_pool.size()) {
            report.add(ir::VerifyErrorCode::SymbolOwnerModuleRefOutOfRange, "symbol owner module sid out of range",
                       symbol_index);
        }
        if (symbol_record.is_exported && !exported_symbol_name_sids.insert(symbol_record.sym_name_sid).second) {
            report.add(ir::VerifyErrorCode::DuplicateExportedSymbolName, "duplicate exported symbol name", symbol_index);
        }
    }
}

void registerVerifierExtensions() { ir::registerDomainVerifyAppendFn(appendDomainIRChecks); }
} // namespace niki::l1_domain

