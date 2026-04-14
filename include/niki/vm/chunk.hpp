#pragma once

#include "value.hpp"
#include <cstdint>
#include <string>
#include <vector>


namespace niki {
struct Chunk {
    std::vector<uint8_t> code;
    std::vector<vm::Value> constants;
    std::vector<uint32_t> lines;
    std::vector<uint32_t> columns;
    std::vector<std::string> string_pool; // 完整方案：携带一份属于本 Chunk 的只读字符串池
};
} // namespace niki
