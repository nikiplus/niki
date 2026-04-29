#pragma once

#include "value.hpp"
#include <cstdint>
#include <string>
#include <vector>


namespace niki {
/*
 * chunk.hpp —— VM 可执行字节码块。
 *
 * Chunk 是“可顺序执行的最小程序单元”：code 保存 opcode 与操作数字节流，
 * constants 保存立即数池，lines/columns 让运行时错误可映射回源码坐标，
 * string_pool 让函数名/符号名在运行期可逆向到文本。
 *
 * 设计上它刻意保持“扁平数组 + 索引”形式，便于解释器进行缓存友好的线性取指，
 * 也便于 lowering 阶段一次性写入并在调试时做位置回溯。
 */
struct Chunk {
    std::vector<uint8_t> code;       ///< 字节码流（opcode + 操作数字节）。
    std::vector<vm::Value> constants; ///< 常量池，按索引被 OP_LOAD_CONST(_W) 读取。
    std::vector<uint32_t> lines;      ///< 与 code 同步的源码行号。
    std::vector<uint32_t> columns;    ///< 与 code 同步的源码列号。
    std::vector<std::string> string_pool; ///< 本 chunk 相关字符串池（函数名/符号名等）。
    /// 本 chunk 编译期观测到的寄存器槽位上界（0..256）。
    /// 0 表示未知，VM 运行时会退回保守上限策略。
    uint16_t max_register_slots = 0;
};
} // namespace niki
