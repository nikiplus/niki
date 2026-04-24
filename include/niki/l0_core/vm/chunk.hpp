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
    /// 本 chunk 编译期观测到的寄存器槽位数（0..256），供 VM 校验物理栈窗口；0 表示未知（VM 按保守上界处理）。
    uint16_t max_register_slots = 0;
};
} // namespace niki
