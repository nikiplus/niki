#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>
#include <cstdlib>

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
    case NodeType::ArrayExpr:
        return compileArrayExpr(exprIdx);
    case NodeType::MapExpr:
        return compileMapExpr(exprIdx);
    case NodeType::IndexExpr:
        return compileIndexExpr(exprIdx);
    case NodeType::CallExpr:
        return compileCallExpr(exprIdx);
    case NodeType::MemberExpr:
        return compileMemberExpr(exprIdx);
    case NodeType::DispatchExpr:
        return compileDispatchExpr(exprIdx);
    case NodeType::ClosureExpr:
        return compileClosureExpr(exprIdx);
    case NodeType::AwaitExpr:
        return compileAwaitExpr(exprIdx);
    case NodeType::BorrowExpr:
        return compileBorrowExpr(exprIdx);
    case NodeType::ImplicitCastExpr:
        return compileImplicitCastExpr(exprIdx);
    default:
        reportError(line, column, "Expected an expression.");
        return ExprResult{0, true};
    }
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
    case TokenType::KW_TRUE:
        emitOp(vm::OPCODE::OP_TRUE, resultReg.reg, line, column);
        break;
    case TokenType::KW_FALSE:
        emitOp(vm::OPCODE::OP_FALSE, resultReg.reg, line, column);
        break;
    case TokenType::KW_NIL:
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
ExprResult Compiler::compileArrayExpr(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    uint32_t arr_len = node.payload.list.elements.length;
    uint8_t arrayReg = regAlloc.allocate();

    // 限制预分配容量在 255 以内
    uint8_t initial_capacity = arr_len > 255 ? 255 : static_cast<uint8_t>(arr_len);

    // OP_NEW_ARRAY R_dst, initial_capacity
    emitOp(vm::OPCODE::OP_NEW_ARRAY, arrayReg, initial_capacity, line, column);

    for (uint32_t i = 0; i < arr_len; ++i) {
        ASTNodeIndex elementIdx = {node.payload.list.elements.start_index + i};
        ExprResult res = compileExpression(elementIdx);

        emitOp(vm::OPCODE::OP_PUSH_ARRAY, arrayReg, res.reg, line, column);

        if (res.is_temp) {
            freeIfTemp(res);
        }
    }

    return {arrayReg, true};
}
ExprResult Compiler::compileMapExpr(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;
    uint32_t map_idx = node.payload.map.map_data_index;
    const MapData &map_data = currentPool->map_data[map_idx];
    uint32_t entry_count = map_data.keys.length;
    uint8_t mapReg = regAlloc.allocate();

    uint8_t initial_capacity = entry_count > 255 ? 255 : static_cast<uint8_t>(entry_count);

    emitOp(vm::OPCODE::OP_NEW_MAP, mapReg, initial_capacity, line, column);
    for (uint32_t i = 0; i < entry_count; ++i) {
        ASTNodeIndex keyIdx = {map_data.keys.start_index + i};
        ASTNodeIndex valIdx = {map_data.values.start_index + i};

        ExprResult keyRes = compileExpression(keyIdx);
        ExprResult valRes = compileExpression(valIdx);

        emitOp(vm::OPCODE::OP_SET_MAP, mapReg, keyRes.reg, valRes.reg, line, column);

        freeIfTemp(keyRes);
        freeIfTemp(valRes);
    }

    return {mapReg, true};
}
ExprResult Compiler::compileIndexExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
// 对象与方法
ExprResult Compiler::compileCallExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
ExprResult Compiler::compileMemberExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
ExprResult Compiler::compileDispatchExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
// 闭包与高级特性
ExprResult Compiler::compileClosureExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
ExprResult Compiler::compileAwaitExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
ExprResult Compiler::compileBorrowExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
ExprResult Compiler::compileImplicitCastExpr(ASTNodeIndex nodeIdx) { return {0, true}; }

} // namespace niki::syntax