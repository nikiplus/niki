#pragma once
#include "niki/vm/chunk.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/value.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <unordered_map>
#include <vector>

namespace niki::vm {

// 调用栈帧
struct CallFrame {
    ObjFunction *function; // 当前正在执行的函数
    uint8_t *ip;           // 指令指针
    size_t base_register;  // 当前帧在全局栈中的起始偏移量（物理寄存器的零点）
    uint8_t out_register;  // Caller 指定的返回值存放寄存器
};

enum class InterpretResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR
};

class VM {
  public:
    VM() = default;
    std::expected<Value, InterpretResult> interpret(const Chunk &chunk, bool is_repl = false);

  private:
    std::array<Value, 8192> stack{}; // 全局物理大栈
    std::vector<CallFrame> frames;   // 调用栈帧，通常最大深度为 64 或 256

    // --- 全局函数表（MVP） ---
    std::unordered_map<uint32_t, ObjFunction *> globals;

    const std::vector<std::string> *current_string_pool = nullptr; // 用于运行时打印函数名等调试信息

    // 当前正在执行的栈帧快捷引用
    CallFrame *currentFrame = nullptr;

    // 内部运行核心循环
    std::expected<Value, InterpretResult> run(bool should_print = false);

    // 快捷访问当前帧的寄存器 0 的物理地址
    Value *currentRegisters() { return &stack[currentFrame->base_register]; }

    uint8_t readByte() { return *(currentFrame->ip)++; };
    /*画个图来理解readShort
    startip(s) = 0
    currentip(C) = startip+2
     s+2=c
    +↓---↓--
    |0|1|2|3|...
    startip被覆盖——因为ip +=2，事实上是不存在startip这个玩意儿的，现在只有一个指针指着2的位置。
        ip
    +----↓--
    |0|1|2|3|...
    现在我要获得0~1这一段的数据。
    那么这个数据的视图的起始位置就是我这个指针往回再退两格(因为刚往前走了两格)
    startip = ip-2
     s  ip
    +↓---↓--
    |0|1|2|3|...
    这个视图的结束位置endip就是我当前位置ip-1(因为我的指针指向的是需要被读取的指令的后一位)
     s e ip
    +↓-↓-↓--
    |0|1|2|3|...
    然后把startip得到的数据压入高八位，和e的数据拼在一起，我们就得到了这一段的数据视图。
             |<-   start ip  ->|<-       end ip     ->|
    uint16_t:|0|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|
    ——为什么不直接+1-1？
    那就是写成ip+= 1
    static_cast<uint16_t>((ip[-1] << 8) | ip[0])
    ip+=1
    return uint16_t;
    这样我们的指针就得动两次，这在vm这种高计算量场景下太贵了——而且我们一般也不会这样写
    */
    /* 嫌废话太多可以直接看这条
    读取 16 位宽操作数（采用大端序 Big-Endian）

    内存布局 (ip 初始指向高位字节):
    +-------+-------+-------+
    | 高8位 | 低8位 | 下一指令...
    +-------+-------+-------+
      ip[0]   ip[1]   ip[2]

    1. 性能优化：直接 ip += 2，只进行一次指针写操作。
    2. 此时 ip 指向了 ip[2]（下一条指令的开头）。
    3. 回溯读取：
       ip[-2] 即原来的 ip[0] (高 8 位)。我们将其 << 8 移至高位。
       ip[-1] 即原来的 ip[1] (低 8 位)。直接按位或（|）拼入低位。

    为什么使用大端序？
    在 dump 字节码时，如果内存是 0x01 0xFF，大端序拼装后是 255 (0x01FF)，
    符合人类从左到右阅读的直觉，极大降低 Debug 认知负担。
    */
    uint16_t readShort() {
        currentFrame->ip += 2;
        return static_cast<uint16_t>((currentFrame->ip[-2] << 8) | currentFrame->ip[-1]);
    }

    Value readConstant(uint8_t index) { return currentFrame->function->chunk.constants[index]; };

    void runtime_error(const char *format, ...);
};
} // namespace niki::vm