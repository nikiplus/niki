#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace niki::syntax {

// Driver 级字符串驻留表：在一次项目编译生命周期内统一 name_id。
class GlobalInterner {
  public:
    GlobalInterner();

    uint32_t intern(std::string_view str);
    std::optional<uint32_t> find(std::string_view str) const;
    const std::string &get(uint32_t id) const;
    std::vector<std::string> snapshot() const;

  private:
    std::deque<std::string> pool;
    std::unordered_map<std::string_view, uint32_t> str_to_id;
};

} // namespace niki::syntax
