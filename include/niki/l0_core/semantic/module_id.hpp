#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace niki {

enum class Kind {
    Function,
    Struct,
    TypeAlias
};

/** @module_id: 模块稳定身份分配器
 *
 * 在全链路编译过程中，模块需要一个稳定且唯一的整数标识符，而非依赖于文件收集顺序的临时索引。
 * ModuleIdAllocator 在 FileScan 阶段建立 source_path -> ModuleId 的幂等映射，确保：
 * - 相同路径总是返回相同 ModuleId
 * - 单元测试可通过固定路径 "__test__" 获得稳定 id
 * - 后续所有阶段以 ModuleId 替代 source_path 字符串进行模块身份判定
 *
 * 这是整个模块体系重构的基石：所有后续的 ModuleNamespace、TypeArena、ModuleRegistry
 * 都将以 ModuleId 替代原有的 owner_module 字符串。
 */

using ModuleId = uint32_t;
constexpr ModuleId kInvalidModuleId = UINT32_MAX;

class ModuleIdAllocator {
  public:
    /// @brief 为 source_path 分配稳定 ModuleId（幂等）。
    /// @return 首次分配返回新 id，重复调用返回已分配的 id。
    ModuleId ensure(const std::string &source_path);

    /// @brief 通过 id 反查 source_path。
    /// @return 找到返回指针，否则返回 nullptr。
    const std::string *findPath(ModuleId id) const;

    /// @brief 当前已分配的模块数量。
    size_t size() const { return paths_.size(); }

  private:
    std::vector<std::string> paths_;                       // id → path
    std::unordered_map<std::string, ModuleId> path_to_id_; // path → id
};

} // namespace niki
