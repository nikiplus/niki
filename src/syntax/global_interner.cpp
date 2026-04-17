#include "niki/syntax/global_interner.hpp"

#include <stdexcept>

namespace niki::syntax {

GlobalInterner::GlobalInterner() {
    // 固化内置类型 ID，确保不同模块在同一编译会话内一致。
    intern("int");
    intern("float");
    intern("bool");
    intern("string");
}

uint32_t GlobalInterner::intern(std::string_view str) {
    auto found_entry = str_to_id.find(str);
    if (found_entry != str_to_id.end()) {
        return found_entry->second;
    }
    uint32_t id = static_cast<uint32_t>(pool.size());
    pool.emplace_back(str);
    str_to_id.emplace(pool.back(), id);
    return id;
}

std::optional<uint32_t> GlobalInterner::find(std::string_view str) const {
    auto found_entry = str_to_id.find(str);
    if (found_entry == str_to_id.end()) {
        return std::nullopt;
    }
    return found_entry->second;
}

const std::string &GlobalInterner::get(uint32_t id) const {
    if (id >= pool.size()) {
        throw std::out_of_range("GlobalInterner id out of range.");
    }
    return pool[id];
}

std::vector<std::string> GlobalInterner::snapshot() const {
    std::vector<std::string> out;
    out.reserve(pool.size());
    for (const auto &item : pool) {
        out.push_back(item);
    }
    return out;
}

} // namespace niki::syntax
