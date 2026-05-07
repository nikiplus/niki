#include "niki/l0_core/semantic/module_namespace.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>

/** @module_namespace_impl: ModuleNamespace 实现
 *
 * insert(): 仅检查同模块内 name_id 是否重复，跨模块同名允许。
 * find():    按 (module_id, name_id) 复合 key 精确查询。
 * findModuleSymbols(): 通过反向 key 索引实现 O(#symbols_in_module) 查询。
 *
 * 关键设计决策：module_symbols_ 存储 Key 而非指针，避免 insert 触发 rehash
 * 导致指针失效。
 */
namespace niki {

bool ModuleNamespace::insert(Symbol sym) {
    Key key{sym.owner_module_id, sym.name_id};
    auto [it, inserted] = symbols_.try_emplace(key, std::move(sym));
    if (!inserted) {
        return false; // 同模块内重名
    }
    // 维护反向索引
    module_symbols_[key.module_id].push_back(key);
    return true;
}

const ModuleNamespace::Symbol *ModuleNamespace::find(ModuleId module_id, uint32_t name_id) const {
    Key key{module_id, name_id};
    auto it = symbols_.find(key);
    if (it != symbols_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<const ModuleNamespace::Symbol *> ModuleNamespace::findModuleSymbols(ModuleId module_id) const {
    std::vector<const Symbol *> result;
    auto it = module_symbols_.find(module_id);
    if (it == module_symbols_.end()) {
        return result;
    }
    result.reserve(it->second.size());
    for (const auto &key : it->second) {
        auto sym_it = symbols_.find(key);
        if (sym_it != symbols_.end()) {
            result.push_back(&sym_it->second);
        }
    }
    return result;
}

} // namespace niki
