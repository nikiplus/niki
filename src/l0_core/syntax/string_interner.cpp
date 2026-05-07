#include "niki/l0_core/syntax/string_interner.hpp"

#include <cstdio>
#include <cstdlib>

namespace niki::syntax {

StringInterner::StringInterner() {
    // 固化内置类型 ID，确保不同模块在同一编译会话内一致。
    intern("int");
    intern("float");
    intern("bool");
    intern("string");
}

/**
 * @brief 驻留字符串并返回稳定 id。
 * @param str 输入字符串视图。
 * @return 已存在返回原 id，否则分配新 id。
 */
uint32_t StringInterner::intern(std::string_view str) {
    auto found_entry = str_to_id.find(str);
    if (found_entry != str_to_id.end()) {
        return found_entry->second;
    }
    uint32_t id = static_cast<uint32_t>(pool.size());
    pool.emplace_back(str);
    str_to_id.emplace(pool.back(), id);
    return id;
}

/**
 * @brief 查询字符串 id（不创建新项）。
 * @param str 输入字符串视图。
 * @return 命中返回 id，否则 nullopt。
 */
std::optional<uint32_t> StringInterner::find(std::string_view str) const {
    auto found_entry = str_to_id.find(str);
    if (found_entry == str_to_id.end()) {
        return std::nullopt;
    }
    return found_entry->second;
}

/**
 * @brief 通过 id 反查字符串。
 * @param id 字符串 id。
 * @return 对应字符串引用；越界时中止进程。
 */
const std::string &StringInterner::get(uint32_t id) const {
    if (id >= pool.size()) {
        std::fprintf(stderr, "StringInterner id out of range.\n");
        std::abort();
    }
    return pool[id];
}

/** @brief 复制导出当前字符串池快照。 */
std::vector<std::string> StringInterner::snapshot() const {
    std::vector<std::string> out;
    out.reserve(pool.size());
    for (const auto &item : pool) {
        out.push_back(item);
    }
    return out;
}

} // namespace niki::syntax
