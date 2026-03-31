#pragma once
#include "ast.hpp"
#include "niki/vm/chunk.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include "token.hpp"

#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace niki::syntax {
struct ExprResult {
    uint8_t reg;
    bool is_temp; // 核心：所有权标记
};
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
    void freeIfTemp(const ExprResult &res);
    uint8_t makeConstant(vm::Value value, uint32_t line, uint32_t column);

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
