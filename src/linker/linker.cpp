#include "niki/linker/linker.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
            uint32_t new_id = static_cast<uint32_t>(out_pool.size());
            out_pool.push_back(interned_string);
            seen.emplace(interned_string, new_id);
        }
    }
}

static std::vector<SymbolDef> collectDefinedSymbols(const CompileModule &module) {
    std::vector<SymbolDef> symbols;
    const auto &string_pool = module.init_chunk.string_pool;

    for (const auto &constant : module.init_chunk.constants) {
        if (constant.type != vm::ValueType::Object || constant.as.object == nullptr) {
            continue;
        }

        auto *object = static_cast<vm::Object *>(constant.as.object);
        if (object->type == vm::ObjType::Function) {
            auto *function_object = static_cast<vm::ObjFunction *>(constant.as.object);
            std::string symbol_name = (function_object->name_id < string_pool.size())
                                          ? string_pool[function_object->name_id]
                                          : ("<id" + std::to_string(function_object->name_id) + ">");
            symbols.push_back(SymbolDef{function_object->name_id, symbol_name, module.source_path});
        } else if (object->type == vm::ObjType::StructDef) {
            auto *struct_definition = static_cast<vm::ObjStructDef *>(constant.as.object);
            std::string symbol_name = (struct_definition->name_id < string_pool.size())
                                          ? string_pool[struct_definition->name_id]
                                          : ("<id" + std::to_string(struct_definition->name_id) + ">");
            symbols.push_back(SymbolDef{struct_definition->name_id, symbol_name, module.source_path});
        }
    }
    return symbols;
}

}; // namespace
std::expected<LinkedProgram, std::vector<LinkError>> Linker::link(const std::vector<CompileModule> &modules,
                                                                  const LinkOptions &options) {
    std::vector<LinkError> errors;

    if (modules.empty()) {
        if (options.allow_no_entry) {
            LinkedProgram empty;
            empty.entry_name_id = UINT32_MAX;
            return empty;
        }
        errors.push_back({LinkErrorCode::ENTRY_NOT_FOUND, "无可链接模块", ""});
        return std::unexpected(std::move(errors));
    }

    LinkedProgram program;
    program.entry_name_id = UINT32_MAX;

    // 1)MVP:多init_chunk:直接保留每个模块的init chunk，运行期按顺序进行
    program.init_chunks.reserve(modules.size());
    for (const auto &module : modules) {
        program.init_chunks.push_back(module.init_chunk);
    }

    // 2)合并字符串池（MVP阶段主要用于诊断和调试输出）
    collectMergedStringPool(modules, program.string_pool);

    // 3)MVP:入口决议+重复符号检查
    // 在共享 interner 下，name_id 已是会话级统一值；这里按“名字是否重复定义”做冲突判断。
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
                errors.push_back({
                    LinkErrorCode::DUPLICATE_SYMBOL,
                    "重复定义符号：\"" + symbol_def.name + "\"",
                    symbol_def.module_path,
                });
            }
            if (symbol_def.name == options.entry_name) {
                entry_id = symbol_def.id;
                entry_count++;
            }
        }
    }

    if (entry_count > 1) {
        errors.push_back({LinkErrorCode::MULTIPLE_ENTRY, "检测到多个入口函数\"" + options.entry_name + "\"", ""});
    } else if (entry_count == 0 && !options.allow_no_entry) {
        errors.push_back({LinkErrorCode::ENTRY_NOT_FOUND, "未找到入口函数\"" + options.entry_name + "\"", ""});
    }

    if (!errors.empty()) {
        return std::unexpected(std::move(errors));
    }
    program.entry_name_id = entry_id;
    return program;
}

// 预留接口：MVP阶段暂不启用
bool Linker::mergeStringPools() { return true; }
bool Linker::remapChunkOperands() { return true; }
bool Linker::resolveSymbols() { return true; }
Chunk Linker::mergeInitChunks() { return Chunk{}; }
} // namespace niki::linker