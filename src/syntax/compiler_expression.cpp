#include "niki/semantic/nktype.hpp"

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

    auto [node, line, column] = getNodeCtx(exprIdx);
    switch (node.type) {
    case NodeType::BinaryExpr:
        return compileBinaryExpr(exprIdx);
    case NodeType::LogicalExpr:
        return compileLogicalExpr(exprIdx);
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
    case NodeType::AwaitExpr:
        return compileAwaitExpr(exprIdx);
    case NodeType::BorrowExpr:
        return compileBorrowExpr(exprIdx);
    case NodeType::ImplicitCastExpr:
        return compileImplicitCastExpr(exprIdx);

    case NodeType::WildcardExpr:
        reportWarning(line, column, "WildcardExpr is a stub.");
        return ExprResult{0, true};
    default:
        reportWarning(line, column, "Unknown Expression Node Type: " + std::to_string(static_cast<int>(node.type)));
        reportError(line, column, "Expected an expression.");
        return ExprResult{0, true};
    }
}

void Compiler::emitBinaryOp(vm::OPCODE int_op, vm::OPCODE float_op, uint8_t resultReg, uint8_t leftReg,
                            uint8_t rightReg, semantic::NKType leftType, semantic::NKType rightType, uint32_t line,
                            uint32_t column, const std::string &opName) {
    if (leftType.getBase() == semantic::NKBaseType::Integer && rightType.getBase() == semantic::NKBaseType::Integer) {
        emitOp(int_op, resultReg, leftReg, rightReg, line, column);
    } else if (leftType.getBase() == semantic::NKBaseType::Float &&
               rightType.getBase() == semantic::NKBaseType::Float) {
        emitOp(float_op, resultReg, leftReg, rightReg, line, column);
    } else {
        reportError(line, column,
                    "Type mismatch for operator '" + opName + "': Expected (Int, Int) or (Float, Float).");
    }
}

// 基础计算
ExprResult Compiler::compileBinaryExpr(ASTNodeIndex nodeIdx) {
    if (!nodeIdx.isvalid()) {
        return {};
    }
    auto [node, line, column] = getNodeCtx(nodeIdx);

    ExprResult leftReg = compileExpression(node.payload.binary.left);
    ExprResult rightReg = compileExpression(node.payload.binary.right);

    semantic::NKType leftType = (*currentTypeTable)[node.payload.binary.left];
    semantic::NKType rightType = (*currentTypeTable)[node.payload.binary.right];
    ExprResult resultReg = {regAlloc.allocate(), true};

    switch (node.payload.binary.op) {
    case TokenType::SYM_PLUS:
        emitBinaryOp(vm::OPCODE::OP_IADD, vm::OPCODE::OP_FADD, resultReg.reg, leftReg.reg, rightReg.reg, leftType,
                     rightType, line, column, "+");
        break;
    case TokenType::SYM_MINUS:
        emitBinaryOp(vm::OPCODE::OP_ISUB, vm::OPCODE::OP_FSUB, resultReg.reg, leftReg.reg, rightReg.reg, leftType,
                     rightType, line, column, "-");
        break;
    case TokenType::SYM_STAR:
        emitBinaryOp(vm::OPCODE::OP_IMUL, vm::OPCODE::OP_FMUL, resultReg.reg, leftReg.reg, rightReg.reg, leftType,
                     rightType, line, column, "*");
        break;
    case TokenType::SYM_SLASH:
        emitBinaryOp(vm::OPCODE::OP_IDIV, vm::OPCODE::OP_FDIV, resultReg.reg, leftReg.reg, rightReg.reg, leftType,
                     rightType, line, column, "/");
        break;
    case TokenType::SYM_MOD:
        if (leftType.getBase() == semantic::NKBaseType::Integer &&
            rightType.getBase() == semantic::NKBaseType::Integer) {
            emitOp(vm::OPCODE::OP_IMOD, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        } else {
            reportError(line, column, "Type mismatch for operator '%': Expected (Int, Int).");
        }
        break;
    case TokenType::SYM_EQUAL_EQUAL:
        emitBinaryOp(vm::OPCODE::OP_IEQ, vm::OPCODE::OP_FEQ, resultReg.reg, leftReg.reg, rightReg.reg, leftType,
                     rightType, line, column, "==");
        break;
    case TokenType::SYM_BANG_EQUAL:
        emitBinaryOp(vm::OPCODE::OP_INE, vm::OPCODE::OP_FNE, resultReg.reg, leftReg.reg, rightReg.reg, leftType,
                     rightType, line, column, "!=");
        break;
    case TokenType::SYM_GREATER:
        emitBinaryOp(vm::OPCODE::OP_IGT, vm::OPCODE::OP_FGT, resultReg.reg, leftReg.reg, rightReg.reg, leftType,
                     rightType, line, column, ">");
        break;
    case TokenType::SYM_LESS:
        emitBinaryOp(vm::OPCODE::OP_ILT, vm::OPCODE::OP_FLT, resultReg.reg, leftReg.reg, rightReg.reg, leftType,
                     rightType, line, column, "<");
        break;
    case TokenType::SYM_LESS_EQUAL:
        emitBinaryOp(vm::OPCODE::OP_ILE, vm::OPCODE::OP_FLE, resultReg.reg, leftReg.reg, rightReg.reg, leftType,
                     rightType, line, column, "<=");
        break;
    case TokenType::SYM_GREATER_EQUAL:
        emitBinaryOp(vm::OPCODE::OP_IGE, vm::OPCODE::OP_FGE, resultReg.reg, leftReg.reg, rightReg.reg, leftType,
                     rightType, line, column, ">=");
        break;
    case TokenType::SYM_BIT_AND:
        if (leftType.getBase() == semantic::NKBaseType::Integer &&
            rightType.getBase() == semantic::NKBaseType::Integer) {
            emitOp(vm::OPCODE::OP_BIT_AND, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        } else {
            reportError(line, column, "Type mismatch for operator '&': Expected (Int, Int).");
        }
        break;
    case TokenType::SYM_BIT_OR:
        if (leftType.getBase() == semantic::NKBaseType::Integer &&
            rightType.getBase() == semantic::NKBaseType::Integer) {
            emitOp(vm::OPCODE::OP_BIT_OR, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        } else {
            reportError(line, column, "Type mismatch for operator '|': Expected (Int, Int).");
        }
        break;
    case TokenType::SYM_BIT_XOR:
        if (leftType.getBase() == semantic::NKBaseType::Integer &&
            rightType.getBase() == semantic::NKBaseType::Integer) {
            emitOp(vm::OPCODE::OP_BIT_XOR, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        } else {
            reportError(line, column, "Type mismatch for operator '^': Expected (Int, Int).");
        }
        break;
    case TokenType::SYM_BIT_SHL:
        if (leftType.getBase() == semantic::NKBaseType::Integer &&
            rightType.getBase() == semantic::NKBaseType::Integer) {
            emitOp(vm::OPCODE::OP_BIT_SHL, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        } else {
            reportError(line, column, "Type mismatch for operator '<<': Expected (Int, Int).");
        }
        break;
    case TokenType::SYM_BIT_SHR:
        if (leftType.getBase() == semantic::NKBaseType::Integer &&
            rightType.getBase() == semantic::NKBaseType::Integer) {
            emitOp(vm::OPCODE::OP_BIT_SHR, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        } else {
            reportError(line, column, "Type mismatch for operator '>>': Expected (Int, Int).");
        }
        break;
    case TokenType::SYM_CONCAT:
        if (leftType.getBase() == semantic::NKBaseType::String && rightType.getBase() == semantic::NKBaseType::String) {
            emitOp(vm::OPCODE::OP_CONCAT, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        } else {
            reportError(line, column, "Type mismatch for operator '..': Expected (String, String).");
        }
        break;
    default:
        reportError(line, column, "Unknown binary operator.");
        break;
    }
    freeIfTemp(leftReg);
    freeIfTemp(rightReg);
    return ExprResult{resultReg.reg, true};
}

ExprResult Compiler::compileLogicalExpr(ASTNodeIndex nodeIdx) {
    if (!nodeIdx.isvalid()) {
        return {};
    }
    auto [node, line, column] = getNodeCtx(nodeIdx);

    ExprResult leftReg = compileExpression(node.payload.logical.left);
    ExprResult resultReg = {regAlloc.allocate(), true};

    // 对于 && 和 ||，我们使用短路求值。这里我们简单发射对应的逻辑指令，
    // 但未来应该在这里发射 JZ (对于 &&) 或 JNZ (对于 ||) 来跳过右侧表达式的计算。
    // 为了 MVP 进度，我们暂时假设 OP_AND 和 OP_OR 可以处理两个寄存器的值并输出到目标寄存器。
    // TODO: 实现真正的短路跳转逻辑。

    ExprResult rightReg = compileExpression(node.payload.logical.right);

    switch (node.payload.logical.op) {
    case TokenType::SYM_AND:
        emitOp(vm::OPCODE::OP_AND, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_OR:
        emitOp(vm::OPCODE::OP_OR, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    default:
        reportError(line, column, "Unknown logical operator.");
        break;
    }

    freeIfTemp(leftReg);
    freeIfTemp(rightReg);

    return ExprResult{resultReg.reg, true};
}

ExprResult Compiler::compileUnaryExpr(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);

    ExprResult reg = compileExpression(node.payload.unary.operand);
    semantic::NKType opType = (*currentTypeTable)[node.payload.unary.operand];
    ExprResult resultReg = {regAlloc.allocate(), true};

    switch (node.payload.unary.op) {
    case TokenType::SYM_MINUS:
        if (opType.getBase() == semantic::NKBaseType::Integer) {
            emitOp(vm::OPCODE::OP_NEG, resultReg.reg, reg.reg, line, column);
        } else if (opType.getBase() == semantic::NKBaseType::Float) {
            emitOp(vm::OPCODE::OP_FNEG, resultReg.reg, reg.reg, line, column);
        } else {
            reportError(line, column, "Type mismatch for operator '-': Expected Int or Float.");
        }
        break;
    case TokenType::SYM_BANG:
        // 在强类型语言中，逻辑非通常只允许作用于 Bool 类型
        if (opType.getBase() == semantic::NKBaseType::Bool) {
            emitOp(vm::OPCODE::OP_NOT, resultReg.reg, reg.reg, line, column);
        } else {
            reportError(line, column, "Type mismatch for operator '!': Expected Bool.");
        }
        break;
    case TokenType::SYM_BIT_NOT:
        if (opType.getBase() == semantic::NKBaseType::Integer) {
            emitOp(vm::OPCODE::OP_BIT_NOT, resultReg.reg, reg.reg, line, column);
        } else {
            reportError(line, column, "Type mismatch for operator '~': Expected Int.");
        }
        break;
    default:
        reportError(line, column, "Unknown unary operator.");
        break;
    }
    freeIfTemp(reg);
    return ExprResult{resultReg.reg, true};
}

ExprResult Compiler::compileLiteralExpr(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);

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
    auto [node, line, column] = getNodeCtx(nodeIdx);
    uint32_t name_id = node.payload.identifier.name_id;

    // 1. 尝试在局部变量表（当前函数寄存器窗口）中查找
    uint8_t localReg = resolveLocal(name_id, line, column);

    // 2. 如果没找到，退化为尝试从全局函数表加载！
    if (localReg == 255) {
        // 这是对 MVP 阶段的一个巨大架构让步：
        // 在未来的强类型系统中，如果找不到局部变量，TypeChecker 会报错。
        // 但现在，我们要把这个“名字（如 calculate_total）”的信息打包，
        // 在运行时通过 OP_GET_GLOBAL，以 name_id 作为常量的形式，让 VM 去哈希表里找函数。

        // 我们把 name_id 这个数字本身，作为一个常量塞进常量池。
        // 这样 VM 执行 OP_GET_GLOBAL 时，就能拿到 name_id，然后去哈希表查找函数指针。
        vm::Value nameVal = vm::Value::makeInt(name_id);
        uint16_t constIdx = makeConstant(nameVal, line, column);

        uint8_t targetReg = regAlloc.allocate();
        if (constIdx < 255) {
            emitOp(vm::OPCODE::OP_GET_GLOBAL, targetReg, static_cast<uint8_t>(constIdx), line, column);
        } else {
            // MVP 暂时不写宽指令，假设 ID 小于 255
            emitOp(vm::OPCODE::OP_GET_GLOBAL, targetReg, static_cast<uint8_t>(constIdx), line, column);
        }
        return ExprResult{targetReg, true};
    }

    return ExprResult{localReg, false};
}

// 复杂数据结构
ExprResult Compiler::compileArrayExpr(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);

    std::span<const ASTNodeIndex> elements = currentPool->get_list(node.payload.list.elements);
    uint32_t arr_len = static_cast<uint32_t>(elements.size());
    uint8_t arrayReg = regAlloc.allocate();

    // 限制预分配容量在 255 以内
    uint8_t initial_capacity = arr_len > 255 ? 255 : static_cast<uint8_t>(arr_len);

    // OP_NEW_ARRAY R_dst, initial_capacity
    emitOp(vm::OPCODE::OP_NEW_ARRAY, arrayReg, initial_capacity, line, column);

    for (ASTNodeIndex elementIdx : elements) {
        ExprResult res = compileExpression(elementIdx);

        emitOp(vm::OPCODE::OP_PUSH_ARRAY, arrayReg, res.reg, line, column);

        if (res.is_temp) {
            freeIfTemp(res);
        }
    }

    return {arrayReg, true};
}
ExprResult Compiler::compileMapExpr(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    uint32_t map_idx = node.payload.map.map_data_index;
    const MapData &map_data = currentPool->map_data[map_idx];
    std::span<const ASTNodeIndex> keyNodes = currentPool->get_list(map_data.keys);
    std::span<const ASTNodeIndex> valueNodes = currentPool->get_list(map_data.values);
    uint32_t entry_count = static_cast<uint32_t>(keyNodes.size());
    uint8_t mapReg = regAlloc.allocate();

    uint8_t initial_capacity = entry_count > 255 ? 255 : static_cast<uint8_t>(entry_count);

    emitOp(vm::OPCODE::OP_NEW_MAP, mapReg, initial_capacity, line, column);
    if (keyNodes.size() != valueNodes.size()) {
        reportError(line, column, "Map literal keys/values size mismatch.");
        return {mapReg, true};
    }

    for (size_t i = 0; i < keyNodes.size(); ++i) {
        ASTNodeIndex keyIdx = keyNodes[i];
        ASTNodeIndex valIdx = valueNodes[i];

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
    auto [node, line, column] = getNodeCtx(nodeIdx);

    ASTNodeIndex targetIdx = node.payload.index.target;
    ExprResult targetRes = compileExpression(targetIdx);
    ExprResult indexRes = compileExpression(node.payload.index.index);
    uint8_t outReg = regAlloc.allocate();

    // 2. 见证奇迹的时刻！
    // 此时我们不再猜拳，直接问类型表：“喂，刚才 Checker 给 target 打的什么标签？”
    semantic::NKType targetType = (*currentTypeTable)[targetIdx.index];

    // 3. 铁证如山，精准分发
    if (targetType.getBase() == semantic::NKBaseType::Map) {
        emitOp(vm::OPCODE::OP_GET_MAP, outReg, targetRes.reg, indexRes.reg, line, column);
    } else if (targetType.getBase() == semantic::NKBaseType::Array) {
        emitOp(vm::OPCODE::OP_GET_ARRAY, outReg, targetRes.reg, indexRes.reg, line, column);
    } else {
        // 兜底（对于 MVP 阶段无法推断出具体类型的目标，暂且回退到 Array 处理，避免强制报错阻断现有功能）
        emitOp(vm::OPCODE::OP_GET_ARRAY, outReg, targetRes.reg, indexRes.reg, line, column);
    }
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
    auto [node, line, column] = getNodeCtx(nodeIdx);
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
    auto [node, line, column] = getNodeCtx(nodeIdx);

    ExprResult objRes = compileExpression(node.payload.member.object);

    uint16_t propNameId = node.payload.member.property_id;

    reportWarning(line, column, "compileMemberExpr is a stub.");
    uint8_t outReg = regAlloc.allocate();
    emitOp(vm::OPCODE::OP_NIL, outReg, line, column);
    freeIfTemp(objRes);
    return {outReg, true};
}
/*- dispatch 解决的是“ 在接收者上下文中调用方法 ”： obj.method(args...) 。
- 这里不仅是 call，还涉及接收者绑定（类似 this/self）和方法查找规则。
- 没有 dispatch，你只能先 member 再“裸 call”，那会丢掉动态分发和接收者语义边界。
*/
ExprResult Compiler::compileDispatchExpr(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportWarning(line, column, "compileDispatchExpr is a stub.");
    uint8_t outReg = regAlloc.allocate();
    emitOp(vm::OPCODE::OP_NIL, outReg, line, column);
    return {outReg, true};
}
// 闭包与高级特性
ExprResult Compiler::compileAwaitExpr(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportWarning(line, column, "compileAwaitExpr is a stub.");
    uint8_t outReg = regAlloc.allocate();
    emitOp(vm::OPCODE::OP_NIL, outReg, line, column);
    return {outReg, true};
}
ExprResult Compiler::compileBorrowExpr(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportWarning(line, column, "compileBorrowExpr is a stub.");
    uint8_t outReg = regAlloc.allocate();
    emitOp(vm::OPCODE::OP_NIL, outReg, line, column);
    return {outReg, true};
}
ExprResult Compiler::compileImplicitCastExpr(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportWarning(line, column, "compileImplicitCastExpr is a stub.");
    uint8_t outReg = regAlloc.allocate();
    emitOp(vm::OPCODE::OP_NIL, outReg, line, column);
    return {outReg, true};
}

} // namespace niki::syntax
