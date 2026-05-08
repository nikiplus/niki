#include "niki/l0_core/linker/linker_facade.hpp"
#include "niki/l0_core/semantic/module_id.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/** @linker_facade_impl: 多模块链接实现
 * 将编译期模块产物拼装为 LinkedProgram，完成字符串池合并、同模块重复符号检测与入口决议。
 */
namespace niki::linker {
namespace {

struct SymbolDef {
    uint32_t id;
    std::string name;
    std::string module_path;
};

static void collectMergedStringPool(const std::vector<CompileModule> &modules, std::vector<std::string> &out_pool) {
    std::unordered_map<std::string, uint32_t> seen;
    for (const auto &module : modules) {
        for (const auto &interned_string : module.init_chunk.string_pool) {
            if (seen.find(interned_string) != seen.end()) {
                continue;
            }
            const uint32_t new_id = static_cast<uint32_t>(out_pool.size());
            out_pool.push_back(interned_string);
            seen.emplace(interned_string, new_id);
        }
    }
}

static std::vector<SymbolDef> collectDefinedSymbols(const CompileModule &module) {
    std::vector<SymbolDef> symbols;
    const auto &string_pool = module.init_chunk.string_pool;
    symbols.reserve(module.exports.size() + module.exported_symbols.size());

    for (const auto &[local_symbol_id, exported_symbol_id] : module.exports) {
        (void)local_symbol_id;
        std::string symbol_name = (exported_symbol_id < string_pool.size())
                                      ? string_pool[exported_symbol_id]
                                      : ("<id" + std::to_string(exported_symbol_id) + ">");
        symbols.push_back(SymbolDef{exported_symbol_id, std::move(symbol_name), module.source_path});
    }

    for (const auto &sym_record : module.exported_symbols) {
        std::string symbol_name = (sym_record.sym_name_sid < string_pool.size())
                                      ? string_pool[sym_record.sym_name_sid]
                                      : ("<sid" + std::to_string(sym_record.sym_name_sid) + ">");
        symbols.push_back(SymbolDef{sym_record.sym_name_sid, std::move(symbol_name), module.source_path});
    }

    return symbols;
}

static std::expected<LinkedProgram, niki::diagnostic::DiagnosticBag> linkModules(const std::vector<CompileModule> &modules,
                                                                               const LinkOptions &options) {
    niki::diagnostic::DiagnosticBag diagnostics;
    if (modules.empty()) {
        diagnostics.error(niki::diagnostic::events::LinkerCode::EntryNotFound, "No modules to link.");
        return std::unexpected(std::move(diagnostics));
    }

    LinkedProgram program;
    program.entry_name_id = UINT32_MAX;
    program.init_chunks.reserve(modules.size());
    for (const auto &module : modules) {
        program.init_chunks.push_back(module.init_chunk);
    }
    collectMergedStringPool(modules, program.string_pool);

    struct SymbolKey {
        ModuleId module_id;
        std::string name;
        bool operator==(const SymbolKey &o) const { return module_id == o.module_id && name == o.name; }
    };
    struct SymbolKeyHash {
        size_t operator()(const SymbolKey &k) const {
            return std::hash<uint64_t>{}((static_cast<uint64_t>(k.module_id) << 32) ^ std::hash<std::string>{}(k.name));
        }
    };
    std::unordered_map<SymbolKey, std::string, SymbolKeyHash> symbol_def_map;
    uint32_t entry_id = UINT32_MAX;
    ModuleId entry_module_id = kInvalidModuleId;
    int entry_count = 0;
    for (const auto &module : modules) {
        auto defined_symbols = collectDefinedSymbols(module);
        for (const auto &symbol_def : defined_symbols) {
            SymbolKey key{module.module_id, symbol_def.name};
            auto [it, inserted] = symbol_def_map.try_emplace(key, symbol_def.module_path);
            if (!inserted) {
                diagnostics.error(niki::diagnostic::events::LinkerCode::DuplicateSymbol,
                                  "Duplicate symbol in same module: \"" + symbol_def.name + "\"",
                                  niki::diagnostic::makeSourceSpan(symbol_def.module_path));
            }
            if (symbol_def.name == options.entry_name) {
                entry_id = symbol_def.id;
                entry_module_id = module.module_id;
                entry_count++;
            }
        }
    }

    if (entry_count > 1) {
        diagnostics.error(niki::diagnostic::events::LinkerCode::MultipleEntry,
                          "Multiple entry functions named \"" + options.entry_name + "\".");
    } else if (entry_count == 0) {
        diagnostics.error(niki::diagnostic::events::LinkerCode::EntryNotFound,
                          "Entry function \"" + options.entry_name + "\" not found.");
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    program.entry_name_id = entry_id;
    program.entry_module_id = entry_module_id;
    return program;
}

} // namespace

std::expected<LinkedProgram, niki::diagnostic::DiagnosticBag> Linker::link(const std::vector<CompileModule> &modules,
                                                                           const LinkOptions &options) {
    return linkModules(modules, options);
}

} // namespace niki::linker
