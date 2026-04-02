#pragma once
#include "niki/vm/chunk.hpp"
#include "niki/vm/value.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

namespace niki::vm {
enum class InterpretResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR
};

class VM {
  public:
    VM() = default;
    InterpretResult interpret(const Chunk &chunk);

  private:
    const Chunk *currentChunk = nullptr;
    uint8_t *ip = nullptr; // Instruction Pointer
    uint8_t *instructionStart = nullptr;

    std::array<Value, 256> registers{};

    uint8_t readByte() { return *ip++; };
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
        ip += 2;
        return static_cast<uint16_t>((ip[-2] << 8) | ip[-1]);
    }

    Value readConstant(uint8_t index) { return currentChunk->constants[index]; };

    void runtime_error(const char *format, ...);

    InterpretResult run();
};
} // namespace niki::vm