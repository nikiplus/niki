#pragma once
#include "ast.hpp"
#include "niki/vm/chunk.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include "token.hpp"

#include <cstdint>
#include <stdexcept>
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
    bool registers[256] = {false};

  public:
    uint8_t allocate() {
        for (int i = 0; i < 256; ++i) {
            if (!registers[i]) {
                registers[i] = true;
                return i;
            }
        }
        throw std::runtime_error("Register overflow! Expression too complex.");
    }
    void free(uint8_t reg) { registers[reg] = false; }
    void reset() {
        for (int i = 0; i < 256; ++i)
            registers[i] = false;
    }
};

class Compiler {
  public:
    bool compile(const ASTPool &pool, ASTNodeIndex root, niki::Chunk &out_chunk);

  private:
    const ASTPool *currentPool = nullptr;
    niki::Chunk *compilingChunk = nullptr;
    bool hadError = false;
    RegisterAllocator regAlloc;
    std::vector<Local> locals;
    int scopeDepth = 0;

    //---辅助函数---
    void freeIfTemp(const ExprResult &res);
    uint8_t makeConstant(vm::Value value, uint32_t line, uint32_t column);
    void beginScope() { scopeDepth++; };
    void endScope();
    uint8_t resolveLocal(uint32_t name_id, uint32_t line, uint32_t column);

    //---字节码发射器---
    void emitByte(uint8_t byte, uint32_t line, uint32_t column);
    void emitOp(vm::OPCODE op, uint32_t line, uint32_t column);
    void emitOp(vm::OPCODE op, uint8_t a, uint32_t line, uint32_t column);
    void emitOp(vm::OPCODE op, uint8_t a, uint8_t b, uint32_t line, uint32_t column);
    void emitOp(vm::OPCODE op, uint8_t a, uint8_t b, uint8_t c, uint32_t line, uint32_t column);

    //---AST遍历核心(Visitor)---
    ExprResult compileNode(ASTNodeIndex nodeIdx);

    //---表达式编译 (返回装载结果的物理寄存器编号)---
    ExprResult compileExpression(ASTNodeIndex exprIdx);
    void compileExpressionWithTarget(ASTNodeIndex exprIdx, uint8_t targetReg);
    // 基础计算
    ExprResult compileBinaryExpr(ASTNodeIndex nodeIdx);
    ExprResult compileUnaryExpr(ASTNodeIndex nodeIdx);
    ExprResult compileLiteralExpr(ASTNodeIndex nodeIdx);
    ExprResult compileIdentifierExpr(ASTNodeIndex nodeIdx);
    // 复杂数据结构
    ExprResult compileArrayExpr(ASTNodeIndex nodeIdx);
    ExprResult compileMapExpr(ASTNodeIndex nodeIdx);
    ExprResult compileIndexExpr(ASTNodeIndex nodeIdx);
    // 对象与方法
    ExprResult compileCallExpr(ASTNodeIndex nodeIdx);
    ExprResult compileMemberExpr(ASTNodeIndex nodeIdx);
    ExprResult compileDispatchExpr(ASTNodeIndex nodeIdx);
    // 闭包与高级特性
    ExprResult compileClosureExpr(ASTNodeIndex nodeIdx);
    ExprResult compileAwaitExpr(ASTNodeIndex nodeIdx);

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
    void compileMatchCase(ASTNodeIndex nodeIdx);
    // 跳转与中断
    void compileContinueStmt(ASTNodeIndex nodeIdx);
    void compileBreakStmt(ASTNodeIndex nodeIdx);
    void compileReturnStmt(ASTNodeIndex nodeIdx);
    void compileNockStmt(ASTNodeIndex nodeIdx);
    // 组件挂载与卸载
    void compileAttachStmt(ASTNodeIndex nodeIdx);
    void compileDetachStmt(ASTNodeIndex nodeIdx);
    void compileTargetStmt(ASTNodeIndex nodeIdx);
    // 异常处理
    void compileThrowStmt(ASTNodeIndex nodeIdx);
    void compileTryCatchStmt(ASTNodeIndex nodeIdx);
    //---顶层声明编译---
    void compileDeclaration(ASTNodeIndex nodeIdx);

    //---错误处理---
    void reportError(const Token &token, std::string_view message);
    void reportError(uint32_t line, uint32_t column, std::string_view message);
};
} // namespace niki::syntax
