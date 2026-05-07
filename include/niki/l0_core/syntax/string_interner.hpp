#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace niki::syntax {

/*
 * string_interner.hpp —— Driver 级字符串驻留表。
 *
 * 该模块在一次编译会话内维护"字符串 -> 稳定 id"映射，避免各 ASTPool 各自编号导致
 * 跨模块链接阶段出现同名异 id 的不一致问题。
 */
class StringInterner {
  public:
    /** @brief 构造 interner，并预热内置类型名。 */
    StringInterner();

    /** @brief 驻留字符串，已存在则返回旧 id。 */
    uint32_t intern(std::string_view str);
    /** @brief 查询字符串 id（不创建）。 */
    std::optional<uint32_t> find(std::string_view str) const;
    /** @brief 由 id 反查字符串。 */
    const std::string &get(uint32_t id) const;
    /** @brief 导出当前字符串池快照。 */
    std::vector<std::string> snapshot() const;

  private:
    std::deque<std::string> pool; ///< 稳定存储区（deque 保证引用/视图稳定）。
    std::unordered_map<std::string_view, uint32_t> str_to_id; ///< 文本到 id 的索引。
};

} // namespace niki::syntax
