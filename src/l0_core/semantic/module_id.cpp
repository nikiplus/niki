#include "niki/l0_core/semantic/module_id.hpp"

/** @module_id_impl: ModuleIdAllocator 实现。
 *
 * ensure() 是幂等的：第一次调用为 source_path 分配稳定 id，后续调用返回已分配的 id。
 * 分配策略为单调递增，从 0 开始。id 一经分配永不回收，保证全链路唯一性。
 */
namespace niki {

ModuleId ModuleIdAllocator::ensure(const std::string &source_path) {
    auto it = path_to_id_.find(source_path);
    if (it != path_to_id_.end()) {
        return it->second;
    }
    ModuleId new_id = static_cast<ModuleId>(paths_.size());
    paths_.push_back(source_path);
    path_to_id_.emplace(paths_.back(), new_id);
    return new_id;
}

const std::string *ModuleIdAllocator::findPath(ModuleId id) const {
    if (id < paths_.size()) {
        return &paths_[id];
    }
    return nullptr;
}

} // namespace niki
