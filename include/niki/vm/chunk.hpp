#pragma once

#include "value.hpp"
#include <cstdint>
#include <vector>

namespace niki {
struct Chunk {
    std::vector<uint8_t> code;
    std::vector<vm::Value> constants;
    std::vector<uint32_t> lines;
    std::vector<uint32_t> columns;
};
} // namespace niki
