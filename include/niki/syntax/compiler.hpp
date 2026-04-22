#pragma once
#include "ast.hpp"

#include "niki/diagnostic/diagnostic.hpp"
#include "niki/semantic/global_symbol_table.hpp"
#include "niki/semantic/global_type_arena.hpp"
#include "niki/semantic/nktype.hpp"
#include "niki/vm/chunk.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include "token.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace niki::syntax {
/*我们使用寄存器式虚拟机，因此我们需要控制和调度各分配器。
在这种类型的虚拟机中，我们的各类数据是被保存在寄存器中的，因此知道每个数据被保存在哪个寄存器中，其在寄存器中的位置就尤为重要。
因此我们声明一个local结构体，用于记录数据所在位置。
*/
struct Local {
    uint32_t name_id; // AST 中标识符的字符串 ID
    uint8_t reg;      // 绑定的物理寄存器
    int depth;        // 作用域深度（处理大括号块的变量覆盖和销毁）
    bool is_owned;    // 该变量是否为数据所有者？——基本类型和借用引用为false，new出来的对象为true
    bool is_moved;    // 其所有权是否被转移？
};
/*表达式结果的物理载体与生命周期描述符。

在寄存器机器中，表达式求值会返回存放结果的物理寄存器编号。
但编译器必须区分这个寄存器的【所有权】：
1. 临时计算结果（如 1+2 产生的寄存器）：用完即弃，必须立刻 free，否则导致寄存器泄漏。 -> is_temp = true
2. 局部变量读取（如直接读取变量 a 所在的寄存器）：其生命周期由作用域(Scope)管理，绝不能在表达式求值后被 free！ ->
is_temp = false
*/
struct ExprResult {
    uint8_t reg;
    bool is_temp; // 核心：生命周期/所有权标记，决定用完后是否 free
};
/*寄存器分配器
寄存器分配器（Register Allocator）
注意：这不是物理寄存器！这是编译期用于追踪 VM 中 256 个虚拟寄存器状态的“账本”。
true 表示该槽位已被局部变量或临时计算占用，false 表示空闲。
*/
class RegisterAllocator {
    // 首先得让寄存器是空的，因此我们将其设为0
    bool registers[256] = {false};

  public:
    uint8_t allocate() {
        for (int i = 0; i < 256; ++i) {
            if (!registers[i]) {
                registers[i] = true;
                return i;
            }
        }
        std::fprintf(stderr, "Register overflow! Expression too complex.\n");
        std::abort();
    }
    void free(uint8_t reg) { registers[reg] = false; }
    void reset() {
        for (int i = 0; i < 256; ++i)
            registers[i] = false;
    }
};

class Compiler {
  public:
    /*哼哼，std::expected,新时代的好东西(C++23方法)。
    可以看到，我们返回了两个结构体，前者包含了我们实际要发射的信息，而后者则包含了一个错误信息。
    注意，我们在传入参数是，提前传入了一个空的chunk结构体，这是为了防止每次我们调用compile方法时，编译器都会在实际执行时申请一个空间以初始化chunk，这会导致昂贵的内存再分配。
    */
    // 编译总入口：
    // 1) 绑定 AST/type table 上下文
    // 2) 建立顶层 ObjFunction 作为脚本容器
    // 3) 深度遍历 AST 发射字节码
    // 4) 汇总并返回最终 Chunk 或错误池
    std::expected<Chunk, diagnostic::DiagnosticBag> compile(const ASTPool &pool, // 旁侧表等
                                                            ASTNodeIndex root,   // 解析根节点
                                                            const std::vector<semantic::NKType> &typeTable, // 类型表
                                                            Chunk initial_chunk = Chunk{}, // 实际代码块
                                                            const GlobalTypeArena *globalArena = nullptr,
                                                            const GlobalSymbolTable *globalSymbols = nullptr);

  private:
    const ASTPool *currentPool = nullptr;
    const std::vector<semantic::NKType> *currentTypeTable = nullptr; // 类型表
    const GlobalTypeArena *currentGlobalArena = nullptr;
    const GlobalSymbolTable *currentGlobalSymbols = nullptr;
    // 当前正在编译的函数对象和对应的Chunk。
    // 在 MVP 中我们用指针，方便嵌套编译时随时切换上下文
    vm::ObjFunction *compilingFunction = nullptr; // 当前正在编译的函数对象
    Chunk *compilingChunk = nullptr;              // 当前正在编译的字节码块

    // 当前函数的编译状态（通过 pushContext/popContext 自动隔离与切换）
    RegisterAllocator regAlloc;
    std::vector<Local> locals;
    int scopeDepth = 0;

    bool hadError = false;
    diagnostic::DiagnosticBag diagnostics;
    std::array<uint32_t, static_cast<size_t>(vm::OPCODE::OP_COUNT)> opcodeEmitCount{};
    uint32_t warningCount = 0;
    uint32_t traceIndent = 0;
    std::vector<vm::ObjFunction> functions;

    struct LoopContext {
        size_t start_pos;
        std::vector<size_t> break_patches;
    };
    std::vector<LoopContext> loop_stack;

    /*在这里有个关键的设计决策点——constants的索引长度究竟应该是8bit还是16bit
    如果我们为了体积和速度，让索引长度为8bit，那么不可避免的问题就是字面量很快就会达到2^8=256的上限。
    如果我们将所有constants定长为16bit，使其高八位位于b寄存器，低八位位于c寄存器，使用时再把它们拼成一个16位的值可不可行呢？
    |<-   reg:B   ->|<-   reg:B   ->|
    |-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|
    |0|0|0|0|0|0|0|0|0|1|1|1|0|1|0|1|
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    似乎可行！问题似乎解决了！
    不仅如此，我们还可以让所有指令都变成16bit，让他们宽度一致来保证cpu扫描时能连续快速的遍历所有opcode，来保证极高的访问效率！
    然而真的如此吗……？
    ——————
    首先我们要知道，现代cpu的缓存结构类似一个金字塔，越靠近cpu的缓存速度越快，可使用内存越小，而越远离cpu的缓存速度越慢，可用内存越大。
    这也就导致，我们想要导向高效率，就必须在这个狭小的最靠近cpu的地方(l1缓存)存下尽可能多的可执行opcode！
    而我们如果想要让我们的指令都在存在L1=，那么要做的最重要的事就是
    压 缩 指 令 大 小
    不仅要把opcode的大小牢牢限定在8bit=1byte的大小，还要尽可能压缩constants索引的大小！
    那我们返回那个用8bit寄存器储存constant的设计？但这样的话又会很快导致那个上限过少的老问题……能不能结合两个方案？让constant小的时候使用8bit存储，大的时候使用16bit存储……？
    没错！这正式现代绝大多数寄存器都在使用的指令设计方案——变长指令。*/
    //---辅助函数---
    // 这组函数负责“类型驱动发射”“临时寄存器回收”“常量池写入”等基础能力。
    void emitBinaryOp(vm::OPCODE int_op, vm::OPCODE float_op, uint8_t resultReg, uint8_t leftReg, uint8_t rightReg,
                      semantic::NKType leftType, semantic::NKType rightType, uint32_t line, uint32_t column,
                      const std::string &opName);

    void freeIfTemp(const ExprResult &res);
    std::string makeTracePrefix() const;
    uint16_t makeConstant(vm::Value value, uint32_t line, uint32_t column);
    void beginScope() { scopeDepth++; };
    void endScope();
    uint8_t resolveLocal(uint32_t name_id, uint32_t line, uint32_t column);

    size_t currentCodePos() const;
    size_t emitJump(vm::OPCODE op, uint32_t line, uint32_t column);
    size_t emitJump(vm::OPCODE op, uint8_t condReg, uint32_t line, uint32_t column);
    void patchJump(size_t patch_pos, size_t target_pos);
    void emitLoopBack(vm::OPCODE op, size_t target_pos, uint32_t line, uint32_t column);

    void beginLoop(size_t start_pos);
    void addBreakPatch(size_t patch_pos, uint32_t line, uint32_t column);
    void endLoop(size_t loop_end_pos);
    //---编译上下文管理---
    // 嵌套函数编译时保存/恢复寄存器状态、局部变量表和循环补丁信息。
    // 用于保存/恢复编译状态，以支持嵌套函数编译。
    struct CompilerContext {
        vm::ObjFunction *function;
        Chunk *chunk;
        RegisterAllocator regAlloc;
        std::vector<Local> locals;
        int scopeDepth;
        std::vector<LoopContext> loop_stack;
    };
    std::vector<CompilerContext> contextStack;

    void pushContext(vm::ObjFunction *func);
    CompilerContext popContext();
    //---字节码发射器---
    // 所有 opcode 输出都经由 emitOp/emitByte，确保 line/column 元数据一致写入。
    void emitByte(uint8_t byte, uint32_t line, uint32_t column);
    void emitOp(vm::OPCODE op, uint32_t line, uint32_t column);
    void emitOp(vm::OPCODE op, uint8_t operand_a, uint32_t line, uint32_t column);
    void emitOp(vm::OPCODE op, uint8_t operand_a, uint8_t operand_b, uint32_t line, uint32_t column);
    void emitOp(vm::OPCODE op, uint8_t operand_a, uint8_t operand_b, uint8_t operand_c, uint32_t line, uint32_t column);
    void emitOp(vm::OPCODE op, uint8_t operand_a, uint8_t operand_b, uint8_t operand_c, uint8_t operand_d,
                uint32_t line, uint32_t column);
    void emitConstant(vm::Value value, uint8_t targetReg, uint32_t line, uint32_t column);

    struct NodeContext {
        const syntax::ASTNode &node;
        uint32_t line;
        uint32_t column;
    };
    inline NodeContext getNodeCtx(syntax::ASTNodeIndex idx) const {
        return {currentPool->getNode(idx), currentPool->locations[idx.index].line,
                currentPool->locations[idx.index].column};
    }
    //---AST遍历核心(Visitor)---
    void preCompileNode(ASTNodeIndex nodeIdx);
    void preCompileFunctionDecl(ASTNodeIndex nodeIdx);

    void compileNode(ASTNodeIndex nodeIdx);

    //---表达式编译 (返回装载结果的物理寄存器编号)---
    ExprResult compileExpression(ASTNodeIndex exprIdx);
    // 基础计算
    ExprResult compileLiteralExpr(ASTNodeIndex nodeIdx);
    ExprResult compileBinaryExpr(ASTNodeIndex nodeIdx);
    ExprResult compileLogicalExpr(ASTNodeIndex nodeIdx);
    ExprResult compileUnaryExpr(ASTNodeIndex nodeIdx);
    ExprResult compileIdentifierExpr(ASTNodeIndex nodeIdx);
    // 复杂数据结构
    ExprResult compileArrayExpr(ASTNodeIndex nodeIdx);
    ExprResult compileMapExpr(ASTNodeIndex nodeIdx);
    ExprResult compileIndexExpr(ASTNodeIndex nodeIdx);
    // 对象与方法
    ExprResult compileCallExpr(ASTNodeIndex nodeIdx);
    ExprResult compileMemberExpr(ASTNodeIndex nodeIdx);
    ExprResult compileDispatchExpr(ASTNodeIndex nodeIdx);
    // 高级特性
    ExprResult compileAwaitExpr(ASTNodeIndex nodeIdx);
    ExprResult compileBorrowExpr(ASTNodeIndex nodeIdx);
    ExprResult compileWildcardExpr(ASTNodeIndex nodeIdx);
    ExprResult compileImplicitCastExpr(ASTNodeIndex nodeIdx);

    //---语句编译 (不返回物理寄存器)---
    void compileStatement(ASTNodeIndex stmtIdx);
    // 基础语句
    void compileExpressionStmt(ASTNodeIndex nodeIdx);
    void compileAssignmentStmt(ASTNodeIndex nodeIdx);
    void compileVarDeclStmt(ASTNodeIndex nodeIdx);
    void compileConstDeclStmt(ASTNodeIndex nodeIdx);
    void compileBlockStmt(ASTNodeIndex nodeIdx);
    // 控制流
    void compileIfStmt(ASTNodeIndex nodeIdx);
    void compileLoopStmt(ASTNodeIndex nodeIdx);
    void compileMatchStmt(ASTNodeIndex nodeIdx);
    void compileMatchCaseStmt(ASTNodeIndex nodeIdx);
    // 跳转与中断
    void compileContinueStmt(ASTNodeIndex nodeIdx);
    void compileBreakStmt(ASTNodeIndex nodeIdx);
    void compileReturnStmt(ASTNodeIndex nodeIdx);
    void compileNockStmt(ASTNodeIndex nodeIdx);
    // 组件挂载与卸载
    void compileAttachStmt(ASTNodeIndex nodeIdx);
    void compileDetachStmt(ASTNodeIndex nodeIdx);
    void compileTargetStmt(ASTNodeIndex nodeIdx);

    //---顶层声明编译---
    void compileDeclaration(ASTNodeIndex nodeIdx);
    // 基础声明
    void compileFunctionDecl(ASTNodeIndex nodeIdx);
    void compileInterfaceMethod(ASTNodeIndex nodeIdx);
    void compileStructDecl(ASTNodeIndex nodeIdx);
    void compileEnumDecl(ASTNodeIndex nodeIdx);
    void compileTypeAliasDecl(ASTNodeIndex nodeIdx);
    void compileInterfaceDecl(ASTNodeIndex nodeIdx);
    void compileImplDecl(ASTNodeIndex nodeIdx);
    // NIKI特有
    void compileModuleDecl(ASTNodeIndex nodeIdx);
    void compileSystemDecl(ASTNodeIndex nodeIdx);
    void compileComponentDecl(ASTNodeIndex nodeIdx);
    void compileFlowDecl(ASTNodeIndex nodeIdx);
    void compileKitsDecl(ASTNodeIndex nodeIdx);
    void compileTagDecl(ASTNodeIndex nodeIdx);
    void compileTagGroupDecl(ASTNodeIndex nodeIdx);
    // 程序根与错误
    void compileProgramRoot(ASTNodeIndex nodeIdx);
    void compileErrorNode(ASTNodeIndex nodeIdx);

    //---错误与警告处理---
    void reportError(const Token &token, std::string_view message);
    void reportError(uint32_t line, uint32_t column, std::string_view message);
    void reportWarning(uint32_t line, uint32_t column, std::string_view message);
};
} // namespace niki::syntax
