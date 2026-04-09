#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

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
    case TokenType::SYM_LESS_EQUAL:
        emitOp(vm::OPCODE::OP_ILE, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_GREATER_EQUAL:
        emitOp(vm::OPCODE::OP_IGE, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_AND:
        emitOp(vm::OPCODE::OP_AND, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_OR:
        emitOp(vm::OPCODE::OP_OR, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_BIT_AND:
        emitOp(vm::OPCODE::OP_BIT_AND, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_BIT_OR:
        emitOp(vm::OPCODE::OP_BIT_OR, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_BIT_XOR:
        emitOp(vm::OPCODE::OP_BIT_XOR, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_BIT_SHL:
        emitOp(vm::OPCODE::OP_BIT_SHL, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_BIT_SHR:
        emitOp(vm::OPCODE::OP_BIT_SHR, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
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
ExprResult Compiler::compileIndexExpr(ASTNodeIndex nodeIdx) {
    // todo:目前支持只读，后续再扩展map分发
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    ExprResult targetRes = compileExpression(node.payload.index.target);
    ExprResult indexRes = compileExpression(node.payload.index.index);

    uint8_t outReg = regAlloc.allocate();

    emitOp(vm::OPCODE::OP_GET_ARRAY, outReg, targetRes.reg, indexRes.reg, line, column);

    freeIfTemp(targetRes);
    freeIfTemp(indexRes);
    return ExprResult(outReg, true);
}

// 对象与方法
/*call 这块我做个笔记，免得以后自己看懵。
为什么语言里必须有 call？
因为“函数”如果不能被调用，就只是一个名字，根本不会参与执行链路。表达式系统也会断掉：
你只能算 1+2，不能算 f(1+2)；你只能声明能力，不能真正触发能力。

为什么这里这样设计？
1) 先编译 callee：先搞清楚“要调用谁”。
2) 再编译 arguments：参数不是寄存器列表，而是 AST 节点列表。必须逐个 compileExpression，才会变成寄存器里的运行时值。
3) 最后发射 OP_CALL：把 outReg / calleeReg / argc 交给 VM 执行。

这不是网络意义上的“远程调用”，只是运行时内部的一次可调用实体执行。*/
ExprResult Compiler::compileCallExpr(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;
    // 先编译被调用目标（函数值/可调用对象）
    ExprResult calleeRes = compileExpression(node.payload.call.callee);
    // arguments 是 AST 节点列表，不是寄存器列表
    std::span<const ASTNodeIndex> argNodes = currentPool->get_list(node.payload.call.arguments);

    // 当前调用约定把参数数量编码为 uint8_t（0~255）
    if (argNodes.size() > 255) {
        reportError(line, column, "Too many call arguments.");

        freeIfTemp(calleeRes);
        return {0, true};
    }

    std::vector<ExprResult> argTemps;
    argTemps.reserve(argNodes.size());

    // --- 核心修复：强制分配连续的物理寄存器作为参数窗口 ---
    // 为了保证传递给被调用者的参数寄存器是绝对连续的（base+0, base+1, base+2...），
    // 我们必须预先分配这些连续的槽位，然后将表达式的计算结果 MOVE 进去，
    // 而不是让表达式随意返回散落各处的旧局部变量寄存器。
    std::vector<uint8_t> contiguous_arg_regs;
    for (size_t i = 0; i < argNodes.size(); ++i) {
        contiguous_arg_regs.push_back(regAlloc.allocate());
    }
    // 按源码顺序逐个编译参数，并将其结果搬运（MOVE）到我们刚刚分配的连续寄存器中
    for (size_t i = 0; i < argNodes.size(); ++i) {
        ExprResult res = compileExpression(argNodes[i]);
        if (res.reg != contiguous_arg_regs[i]) {
            emitOp(vm::OPCODE::OP_MOVE, contiguous_arg_regs[i], res.reg, line, column);
        }
        freeIfTemp(res);
    }
    uint8_t outReg = regAlloc.allocate();
    // 参数窗口的起始物理地址就是我们预分配的第一个连续寄存器
    uint8_t argStartReg = contiguous_arg_regs.empty() ? 0 : contiguous_arg_regs[0];

    emitOp(vm::OPCODE::OP_CALL, outReg, calleeRes.reg, argStartReg, static_cast<uint8_t>(argNodes.size()), line,
           column);

    freeIfTemp(calleeRes);
    // 调用结束后，清理为参数窗口分配的临时连续寄存器
    for (uint8_t reg : contiguous_arg_regs) {
        regAlloc.free(reg);
    }

    return {outReg, true};
}
/*
- member 解决的是“ 取成员值 ”问题： obj.field / obj.method （先取到成员本身）。
- 没有它，你只能访问裸变量，不能做对象/模块/结构体字段访问。
- 语义上它是“取引用/取值”，不一定执行调用。
*/
ExprResult Compiler::compileMemberExpr(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    ExprResult objRes = compileExpression(node.payload.member.object);

    uint16_t propNameId = node.payload.member.property_id;

    return {0, true};
}
/*- dispatch 解决的是“ 在接收者上下文中调用方法 ”： obj.method(args...) 。
- 这里不仅是 call，还涉及接收者绑定（类似 this/self）和方法查找规则。
- 没有 dispatch，你只能先 member 再“裸 call”，那会丢掉动态分发和接收者语义边界。
*/
ExprResult Compiler::compileDispatchExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
// 闭包与高级特性
ExprResult Compiler::compileClosureExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
ExprResult Compiler::compileAwaitExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
ExprResult Compiler::compileBorrowExpr(ASTNodeIndex nodeIdx) { return {0, true}; }
ExprResult Compiler::compileImplicitCastExpr(ASTNodeIndex nodeIdx) { return {0, true}; }

} // namespace niki::syntax
