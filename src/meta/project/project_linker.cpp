#include "niki/meta/project/project_linker.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>

/** @meta_project_linker_impl: 项目级链接策略实现
 * 该文件负责把编译期模块产物拼装为可执行程序对象，并完成入口与重复符号决议。
 * 它属于元编排策略层，不负责 IR 生成与 VM 执行细节。
 */
namespace niki::meta::project {
namespace {

//------------------------------------------------------------------------------
// LINK_HELPER: 字符池合并与导出符号采集辅助。
//------------------------------------------------------------------------------
struct SymbolDef {
    uint32_t id;
    std::string name;
    std::string module_path;
};

static void collectMergedStringPool(const std::vector<linker::CompileModule> &modules, std::vector<std::string> &out_pool) {
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

static std::vector<SymbolDef> collectDefinedSymbols(const linker::CompileModule &module) {
    std::vector<SymbolDef> symbols;
    const auto &string_pool = module.init_chunk.string_pool;
    symbols.reserve(module.exports.size());

    for (const auto &[local_symbol_id, exported_symbol_id] : module.exports) {
        (void)local_symbol_id;
        std::string symbol_name = (exported_symbol_id < string_pool.size())
                                      ? string_pool[exported_symbol_id]
                                      : ("<id" + std::to_string(exported_symbol_id) + ">");
        symbols.push_back(SymbolDef{exported_symbol_id, std::move(symbol_name), module.source_path});
    }
    return symbols;
}

} // namespace

//------------------------------------------------------------------------------
// LINK_STAGE: 项目链接主流程。
//------------------------------------------------------------------------------
std::expected<linker::LinkedProgram, diagnostic::DiagnosticBag> ProjectLinker::link(
    const std::vector<linker::CompileModule> &modules,
    const linker::LinkOptions &options) {
    diagnostic::DiagnosticBag diagnostics;
    if (modules.empty()) {
        diagnostics.error(diagnostic::events::LinkerCode::EntryNotFound, "No modules to link.");
        return std::unexpected(std::move(diagnostics));
    }

    linker::LinkedProgram program;
    program.entry_name_id = UINT32_MAX;
    program.init_chunks.reserve(modules.size());
    for (const auto &module : modules) {
        program.init_chunks.push_back(module.init_chunk);
    }
    collectMergedStringPool(modules, program.string_pool);

    std::unordered_map<std::string, std::string> name_to_owner;
    uint32_t entry_id = UINT32_MAX;
    int entry_count = 0;
    for (const auto &module : modules) {
        auto defined_symbols = collectDefinedSymbols(module);
        for (const auto &symbol_def : defined_symbols) {
            auto existing_owner = name_to_owner.find(symbol_def.name);
            if (existing_owner == name_to_owner.end()) {
                name_to_owner.emplace(symbol_def.name, symbol_def.module_path);
            } else {
                diagnostics.error(diagnostic::events::LinkerCode::DuplicateSymbol,
                                  "Duplicate symbol: \"" + symbol_def.name + "\"",
                                  diagnostic::makeSourceSpan(symbol_def.module_path));
            }
            if (symbol_def.name == options.entry_name) {
                entry_id = symbol_def.id;
                entry_count++;
            }
        }
    }

    if (entry_count > 1) {
        diagnostics.error(diagnostic::events::LinkerCode::MultipleEntry,
                          "Multiple entry functions named \"" + options.entry_name + "\".");
    } else if (entry_count == 0) {
        diagnostics.error(diagnostic::events::LinkerCode::EntryNotFound,
                          "Entry function \"" + options.entry_name + "\" not found.");
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    program.entry_name_id = entry_id;
    return program;
}

} // namespace niki::meta::project
