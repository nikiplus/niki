#pragma once
#include "niki/l0_core/vm/chunk.hpp"
#include <cstddef>
#include <string_view>

// 反编译器，用来把 IR lowering 产出的字节码反编译回可见文本格式。
/*来讲讲我们是怎么反编译的。
不过在此之前，我们梳理一下整个流程：
@:源代码(文本文件)->Scanner->Token流->Parser->AstPool->IRBuilder->ModuleIR->LowerToChunk->Chunk(code/constants/line/column)
等等？一行文本变来变去，从token流变成ast树，又从树变成opcode流，这不纯脱裤子放屁吗？
事实上，这几次转化有其必要性：Parser 保证语义结构完整，IRBuilder/Verify/Lower 则把语义结构稳定降级为可执行指令流。(具体实现见各模块文档)
闲言少叙，那么已知我们chunk中的四个vector中的数据都是互相隐式映射的，那么按道理来说只要一个偏移值，我们就能找到一个opcode对应的所有数据。

            coming offset↓
struct Chunk{            ↓
                +---+---+↓--+---+---
    opcode:     |op1|op2|op3|op4|..
                +---+---+↓--+---+---
    constants:  |va1|va2|va3|va4|..
                +---+---+↓--+---+---
    line:       |li1|li2|li3|li4|..
                +---+---+↓--+---+---
    column:     |co1|co2|co3|co4|..
                +---+---+---+---+---
};

借由此，我们完全可以通过扫描chunk的方式，将其中的数据一个个提取出来，重新排版，还原为人类可知的信息。
当然，实际的使用中，我们的数组不可能像上面的图示一样整整齐齐（我们只能不断逼近图示的结果以达到最高效率），事实上，数组中是充斥着许多长短不一的数据和空泡的。
因此我们在实际提取时要进行区分，防止我们提取数据后索引发生了与预期不符的偏移（比如本来要指向a+b=c的c，却因为只移动了一位而指向了b）
*/
namespace niki::vm {
class Disassembler {
  public:
    static void disassembleChunk(const Chunk &chunk, std::string_view name);

  private:
    // 读取当前opcode，根据opcode类型调用对应方法。
    static size_t disassembleInstruction(const Chunk &chunk, size_t offset);
    // 打印不带参数的指令(如return，Op_add等)
    static size_t simpleInstruction(const char *name, size_t offset);
    // 处理字面量
    static size_t literalInstruction(const char *name, const Chunk &chunk, size_t offset);
    // 一元表达式
    static size_t unaryInstruction(const char *name, const Chunk &chunk, size_t offset);
    // 跳转相关指令
    static size_t jumpInstruction(const char *name, int sign, const Chunk &chunk, size_t offset);
    // 处理寄存器操作相关指令(如move等)
    static size_t registerInstruction(const char *name, const Chunk &chunk, size_t offset);
    // 处理当指令需要从数组constants中取值时(如OP_LOAD_CONSTANT等)
    static size_t constantInstruction(const char *name, const Chunk &chunk, size_t offset);
    static size_t constantWideInstruction(const char *name, const Chunk &chunk, size_t offset);
};
} // namespace niki::vm