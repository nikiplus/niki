#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>

namespace niki::syntax {

//---表达式编译---
ExprResult Compiler::compileExpression(ASTNodeIndex exprIdx) {
    if (!exprIdx.isvalid()) {
        return {};
    }

    const ASTNode &node = currentPool->getNode(exprIdx);
    uint32_t line = currentPool->locations[exprIdx.index].line;
    uint32_t column = currentPool->locations[exprIdx.index].column;
    switch (node.type) {
    case NodeType::BinaryExpr:
        return compileBinaryExpr(exprIdx);
    case NodeType::UnaryExpr:
        return compileUnaryExpr(exprIdx);
    case NodeType::LiteralExpr:
        return compileLiteralExpr(exprIdx);
    case NodeType::IdentifierExpr:
        return compileIdentifierExpr(exprIdx);
    // ...
    default:
        reportError(line, column, "Expected an expression.");
        return ExprResult{0, true};
    }
}

void Compiler::compileExpressionWithTarget(ASTNodeIndex exprIdx, uint8_t targetReg) {
    if (!exprIdx.isvalid()) {
        return;
    }
    const ASTNode &node = currentPool->getNode(exprIdx);
    uint32_t line = currentPool->locations[exprIdx.index].line;
    uint32_t column = currentPool->locations[exprIdx.index].column;
    if (node.type == NodeType::LiteralExpr) {
        switch (node.payload.literal.literal_type) {
        case TokenType::LITERAL_INT: { // 此处未来可以优化，减少一次搬运
            uint32_t pool_idx = node.payload.literal.const_pool_index;
            vm::Value val = currentPool->constants[pool_idx];
            uint8_t chunk_idx = makeConstant(val, line, column);
            emitOp(vm::OPCODE::OP_LOAD_CONST, targetReg, chunk_idx, line, column);
            return;
        }
        case TokenType::LITERAL_FLOAT: {
            uint32_t pool_idx = node.payload.literal.const_pool_index;
            vm::Value val = currentPool->constants[pool_idx];
            uint8_t chunk_idx = makeConstant(val, line, column);
            emitOp(vm::OPCODE::OP_LOAD_CONST, targetReg, chunk_idx, line, column);
            return;
        }
        case TokenType::LITERAL_STRING: {
            uint32_t pool_idx = node.payload.literal.const_pool_index;
            vm::Value val = currentPool->constants[pool_idx];
            uint8_t chunk_idx = makeConstant(val, line, column);
            emitOp(vm::OPCODE::OP_LOAD_CONST, targetReg, chunk_idx, line, column);
            return;
        }
        case TokenType::KEYWORD_TRUE:
            emitOp(vm::OPCODE::OP_TRUE, targetReg, line, column);
            return;
        case TokenType::KEYWORD_FALSE:
            emitOp(vm::OPCODE::OP_FALSE, targetReg, line, column);
            return;
        case TokenType::NIL:
            emitOp(vm::OPCODE::OP_NIL, targetReg, line, column);
            return;
        default:
            reportError(line, column, "Expected an expression.");
        }
    }
    ExprResult resultReg = compileExpression(exprIdx);
    if (resultReg.reg != targetReg) {
        emitOp(vm::OPCODE::OP_MOVE, targetReg, resultReg.reg, line, column);
    }
    freeIfTemp(resultReg);
}

// 基础计算
ExprResult Compiler::compileBinaryExpr(ASTNodeIndex nodeIdx) {
    if (!nodeIdx.isvalid()) {
        return {};
    }
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    ExprResult leftReg = compileExpression(node.payload.binary.left);
    ExprResult rightReg = compileExpression(node.payload.binary.right);
    ExprResult resultReg = {regAlloc.allocate(), true};

    switch (node.payload.binary.op) {
    case TokenType::SYM_PLUS:
        emitOp(vm::OPCODE::OP_IADD, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_MINUS:
        emitOp(vm::OPCODE::OP_ISUB, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_STAR:
        emitOp(vm::OPCODE::OP_IMUL, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_SLASH:
        emitOp(vm::OPCODE::OP_IDIV, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_MOD:
        emitOp(vm::OPCODE::OP_IMOD, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_EQUAL_EQUAL:
        emitOp(vm::OPCODE::OP_IEQ, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_BANG_EQUAL:
        emitOp(vm::OPCODE::OP_INE, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_GREATER:
        emitOp(vm::OPCODE::OP_IGT, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_LESS:
        emitOp(vm::OPCODE::OP_ILT, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    default:
        reportError(line, column, "Unknown binary operator.");
        break;
    }
    freeIfTemp(leftReg);
    freeIfTemp(rightReg);
    return ExprResult{resultReg.reg, true};
}

ExprResult Compiler::compileUnaryExpr(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    ExprResult reg = compileExpression(node.payload.unary.operand);
    ExprResult resultReg = {regAlloc.allocate(), true};

    switch (node.payload.unary.op) {
    case TokenType::SYM_MINUS:
        emitOp(vm::OPCODE::OP_NEG, resultReg.reg, reg.reg, line, column);
        break;
    case TokenType::SYM_BANG:
        emitOp(vm::OPCODE::OP_NOT, resultReg.reg, reg.reg, line, column);
        break;
    case TokenType::SYM_BIT_NOT:
        emitOp(vm::OPCODE::OP_BIT_NOT, resultReg.reg, reg.reg, line, column);
        break;
    default:
        reportError(line, column, "Unknown unary operator.");
        break;
    }
    freeIfTemp(reg);
    return ExprResult{resultReg.reg, true};
}

ExprResult Compiler::compileLiteralExpr(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    ExprResult resultReg = {regAlloc.allocate(), true};

    switch (node.payload.literal.literal_type) {
    case TokenType::LITERAL_INT:
    case TokenType::LITERAL_FLOAT:
    case TokenType::LITERAL_STRING: {
        uint32_t pool_idx = node.payload.literal.const_pool_index;
        vm::Value val = currentPool->constants[pool_idx];
        uint8_t chunk_idx = makeConstant(val, line, column);
        emitOp(vm::OPCODE::OP_LOAD_CONST, resultReg.reg, chunk_idx, line, column);
        break;
    }
    case TokenType::KEYWORD_TRUE:
        emitOp(vm::OPCODE::OP_TRUE, resultReg.reg, line, column);
        break;
    case TokenType::KEYWORD_FALSE:
        emitOp(vm::OPCODE::OP_FALSE, resultReg.reg, line, column);
        break;
    case TokenType::NIL:
        emitOp(vm::OPCODE::OP_NIL, resultReg.reg, line, column);
        break;
    default:
        reportError(line, column, "Unknow literal type.");
        break;
    }
    return ExprResult{resultReg.reg, true};
}

ExprResult Compiler::compileIdentifierExpr(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;
    uint8_t reg = resolveLocal(node.payload.identifier.name_id, line, column);
    return ExprResult{reg, false};
}

// 复杂数据结构
ExprResult Compiler::compileArrayExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
ExprResult Compiler::compileMapExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
ExprResult Compiler::compileIndexExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
// 对象与方法
ExprResult Compiler::compileCallExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
ExprResult Compiler::compileMemberExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
ExprResult Compiler::compileDispatchExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
// 闭包与高级特性
ExprResult Compiler::compileClosureExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
ExprResult Compiler::compileAwaitExpr(ASTNodeIndex nodeIdx) { return {0, true}; }

} // namespace niki::syntax