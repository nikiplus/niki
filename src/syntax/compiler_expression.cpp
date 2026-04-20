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
// 入口：按节点类型分发到 compileXXXExpr，并返回 ExprResult{reg, is_temp}。
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
    case NodeType::TypeExpr:
        // TypeExpr 仅在 Semantic 阶段用于类型解析，在运行时没有物理值，不应被编译发射！
        reportError(line, column, "Type expression cannot be evaluated at runtime.");
        return ExprResult{0, true};
    case NodeType::WildcardExpr:
        reportError(line, column, "Wildcard expression compilation is not implemented yet.");
        return ExprResult{0, true};
    default:
        reportWarning(line, column, "Unknown Expression Node Type: " + std::to_string(static_cast<int>(node.type)));
        reportError(line, column, "Expected an expression.");
        return ExprResult{0, true};
    }
}

// 移除过度抽象的 emitBinaryOp
// 基础计算
ExprResult Compiler::compileBinaryExpr(ASTNodeIndex nodeIdx) {
    // 流程：编译左右值 -> 依据 type table 选 opcode -> 释放临时寄存器。
    if (!nodeIdx.isvalid()) {
        return {};
    }
    auto [node, line, column] = getNodeCtx(nodeIdx);

    ExprResult leftReg = compileExpression(node.payload.binary.left);
    ExprResult rightReg = compileExpression(node.payload.binary.right);

    semantic::NKType leftType = (*currentTypeTable)[node.payload.binary.left];
    semantic::NKType rightType = (*currentTypeTable)[node.payload.binary.right];
    ExprResult resultReg = {regAlloc.allocate(), true};

    // 提取基底类型，方便后续判断
    auto lBase = leftType.getBase();
    auto rBase = rightType.getBase();

    // 兜底处理：如果 TypeChecker 放过了 Unknown，我们默认当成 Int 发射，避免编译器崩溃
    if (lBase == semantic::NKBaseType::Unknown || rBase == semantic::NKBaseType::Unknown) {
        reportWarning(line, column, "Unknown operand type, fallback to integer opcode.");
        lBase = semantic::NKBaseType::Integer;
        rBase = semantic::NKBaseType::Integer;
    }

    switch (node.payload.binary.op) {
    // --- 算术运算 ---
    case TokenType::SYM_PLUS:
        if (lBase == semantic::NKBaseType::Integer)
            emitOp(vm::OPCODE::OP_IADD, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else if (lBase == semantic::NKBaseType::Float)
            emitOp(vm::OPCODE::OP_FADD, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_MINUS:
        if (lBase == semantic::NKBaseType::Integer)
            emitOp(vm::OPCODE::OP_ISUB, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else if (lBase == semantic::NKBaseType::Float)
            emitOp(vm::OPCODE::OP_FSUB, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_STAR:
        if (lBase == semantic::NKBaseType::Integer)
            emitOp(vm::OPCODE::OP_IMUL, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else if (lBase == semantic::NKBaseType::Float)
            emitOp(vm::OPCODE::OP_FMUL, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_SLASH:
        if (lBase == semantic::NKBaseType::Integer)
            emitOp(vm::OPCODE::OP_IDIV, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else if (lBase == semantic::NKBaseType::Float)
            emitOp(vm::OPCODE::OP_FDIV, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_MOD:
        if (lBase == semantic::NKBaseType::Integer)
            emitOp(vm::OPCODE::OP_IMOD, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;

    // --- 字符串运算 ---
    case TokenType::SYM_CONCAT:
        if (lBase == semantic::NKBaseType::String)
            emitOp(vm::OPCODE::OP_CONCAT, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;

    // --- 比较运算 ---
    case TokenType::SYM_EQUAL_EQUAL:
        if (lBase == semantic::NKBaseType::Integer)
            emitOp(vm::OPCODE::OP_IEQ, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else if (lBase == semantic::NKBaseType::Float)
            emitOp(vm::OPCODE::OP_FEQ, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else if (lBase == semantic::NKBaseType::String)
            emitOp(vm::OPCODE::OP_SEQ, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else
            emitOp(vm::OPCODE::OP_OEQ, resultReg.reg, leftReg.reg, rightReg.reg, line, column); // 对象比较
        break;
    case TokenType::SYM_BANG_EQUAL:
        if (lBase == semantic::NKBaseType::Integer)
            emitOp(vm::OPCODE::OP_INE, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else if (lBase == semantic::NKBaseType::Float)
            emitOp(vm::OPCODE::OP_FNE, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else if (lBase == semantic::NKBaseType::String)
            emitOp(vm::OPCODE::OP_SNE, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else
            emitOp(vm::OPCODE::OP_ONE, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_LESS:
        if (lBase == semantic::NKBaseType::Integer)
            emitOp(vm::OPCODE::OP_ILT, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else if (lBase == semantic::NKBaseType::Float)
            emitOp(vm::OPCODE::OP_FLT, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_LESS_EQUAL:
        if (lBase == semantic::NKBaseType::Integer)
            emitOp(vm::OPCODE::OP_ILE, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else if (lBase == semantic::NKBaseType::Float)
            emitOp(vm::OPCODE::OP_FLE, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_GREATER:
        if (lBase == semantic::NKBaseType::Integer)
            emitOp(vm::OPCODE::OP_IGT, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else if (lBase == semantic::NKBaseType::Float)
            emitOp(vm::OPCODE::OP_FGT, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_GREATER_EQUAL:
        if (lBase == semantic::NKBaseType::Integer)
            emitOp(vm::OPCODE::OP_IGE, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        else if (lBase == semantic::NKBaseType::Float)
            emitOp(vm::OPCODE::OP_FGE, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;

    // --- 位运算 ---
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
        reportError(line, column, "Unsupported binary operator in compilation.");
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

    // 1. 先计算左操作数
    ExprResult leftReg = compileExpression(node.payload.logical.left);

    // 2. 准备存放最终结果的寄存器
    ExprResult resultReg = {regAlloc.allocate(), true};

    // 3. 将左侧的结果先放进最终寄存器里（以备短路时直接返回）
    emitOp(vm::OPCODE::OP_MOVE, resultReg.reg, leftReg.reg, line, column);

    // 4. 根据操作符决定跳转条件
    size_t patch_pos;
    if (node.payload.logical.op == TokenType::SYM_AND) {
        // 对于 && (AND)：如果左边是 false (0)，直接跳过右边的计算 (OP_JZ)
        patch_pos = emitJump(vm::OPCODE::OP_JZ, resultReg.reg, line, column);
    } else if (node.payload.logical.op == TokenType::SYM_OR) {
        // 对于 || (OR)：如果左边是 true (非0)，直接跳过右边的计算 (OP_JNZ)
        patch_pos = emitJump(vm::OPCODE::OP_JNZ, resultReg.reg, line, column);
    } else {
        reportError(line, column, "Unknown logical operator.");
        return resultReg;
    }

    // --- 这里是被跳转保护的“右侧表达式”区域 ---
    // 5. 只有没有被短路跳过，VM 才会执行到这里，编译右侧表达式
    ExprResult rightReg = compileExpression(node.payload.logical.right);

    // 6. 如果执行了右侧，那么整个逻辑表达式的最终结果就是右侧的结果
    emitOp(vm::OPCODE::OP_MOVE, resultReg.reg, rightReg.reg, line, column);
    freeIfTemp(rightReg);

    // --- 保护区结束 ---
    // 7. 回填跳转指令的目标地址，让短路跳跃直接落在这里
    patchJump(patch_pos, currentCodePos());

    freeIfTemp(leftReg);
    return resultReg;
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
        // 与 VM 的真值语义保持一致：! 支持 Bool 和 Int
        if (opType.getBase() == semantic::NKBaseType::Bool || opType.getBase() == semantic::NKBaseType::Integer) {
            emitOp(vm::OPCODE::OP_NOT, resultReg.reg, reg.reg, line, column);
        } else {
            reportError(line, column, "Type mismatch for operator '!': Expected Bool or Int.");
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
        uint32_t constIdx = makeConstant(nameVal, line, column);

        uint8_t targetReg = regAlloc.allocate();
        if (constIdx <= 0xFF) {
            emitOp(vm::OPCODE::OP_GET_GLOBAL, targetReg, static_cast<uint8_t>(constIdx), line, column);
        } else if (constIdx <= 0xFFFF) {
            emitOp(vm::OPCODE::OP_GET_GLOBAL_W, targetReg, static_cast<uint16_t>(constIdx), line, column);
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
    // 流程：编译 callee/args -> 参数搬运到连续窗口 -> 发射 OP_CALL/OP_NEW_INSTANCE。
    auto [node, line, column] = getNodeCtx(nodeIdx);
    // 先编译被调用目标（可能是函数，也可能是结构体名）
    ExprResult calleeRes = compileExpression(node.payload.call.callee);

    // 查询被调用目标的类型，判断它是函数调用还是结构体实例化！
    semantic::NKType calleeType = currentPool->node_types[node.payload.call.callee.index];

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

    // --- 强制分配连续的物理寄存器作为参数窗口 ---
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

    // 如果 callee 的类型是 Object（即它是一个结构体蓝图），我们发射 OP_NEW_INSTANCE！
    // 否则（如 Function 或 Unknown 兜底），我们发射普通的 OP_CALL。
    if (calleeType.getBase() == semantic::NKBaseType::Object) {
        emitOp(vm::OPCODE::OP_NEW_INSTANCE, outReg, calleeRes.reg, argStartReg, static_cast<uint8_t>(argNodes.size()),
               line, column);
    } else {
        emitOp(vm::OPCODE::OP_CALL, outReg, calleeRes.reg, argStartReg, static_cast<uint8_t>(argNodes.size()), line,
               column);
    }

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
    // 策略：静态 struct 走 OP_GET_FIELD；动态属性访问仍为未实现路径。
    auto [node, line, column] = getNodeCtx(nodeIdx);

    // 1. 编译对象本身 (比如 p.hp 中的 p)
    ExprResult objRes = compileExpression(node.payload.member.object);
    uint32_t propNameId = node.payload.member.property_id;

    // 2. 查表：p 是什么类型？
    semantic::NKType objType = currentPool->node_types[node.payload.member.object.index];

    uint8_t outReg = regAlloc.allocate();

    if (objType.getBase() == semantic::NKBaseType::Object) {
        uint32_t struct_id = static_cast<uint32_t>(objType.getTypeId()); // GlobalTypeArena::StructInfo 索引
        if (currentGlobalArena == nullptr) {
            reportError(line, column, "Global type arena is not available in compiler.");
        } else if (const auto *struct_info = currentGlobalArena->findStruct(struct_id); struct_info != nullptr) {
            uint8_t field_index = 255;
            for (size_t i = 0; i < struct_info->field_name_ids.size(); ++i) {
                if (struct_info->field_name_ids[i] == propNameId) {
                    if (i > 254) {
                        reportError(line, column, "Struct has too many fields (max 254 supported).");
                        freeIfTemp(objRes);
                        return {outReg, true};
                    }
                    field_index = static_cast<uint8_t>(i);
                    break;
                }
            }

            if (field_index == 255) {
                reportError(line, column, "Struct does not have this field.");
            } else {
                // 3. 完美发射 O(1) 的物理字段访问指令！
                emitOp(vm::OPCODE::OP_GET_FIELD, outReg, objRes.reg, field_index, line, column);
            }
        } else {
            reportError(line, column, "Invalid struct type id.");
        }
    } else {
        // 如果不是静态已知的结构体，或者是 map 字典，
        // 我们需要回退到动态的 OP_GET_PROPERTY 字符串哈希查找（MVP暂不实现）
        reportError(line, column, "Dynamic property access on non-struct objects is not implemented yet.");
        emitOp(vm::OPCODE::OP_NIL, outReg, line, column);
    }

    freeIfTemp(objRes);
    return {outReg, true};
}
/*- dispatch 解决的是“ 在接收者上下文中调用方法 ”： obj.method(args...) 。
- 这里不仅是 call，还涉及接收者绑定（类似 this/self）和方法查找规则。
- 没有 dispatch，只能先 member 再“裸 call”，那会丢掉动态分发和接收者语义边界。
*/
ExprResult Compiler::compileDispatchExpr(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Dispatch expression compilation is not implemented yet.");
    uint8_t outReg = regAlloc.allocate();
    emitOp(vm::OPCODE::OP_NIL, outReg, line, column);
    return {outReg, true};
}
// 高级特性
ExprResult Compiler::compileAwaitExpr(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Await expression compilation is not implemented yet.");
    uint8_t outReg = regAlloc.allocate();
    emitOp(vm::OPCODE::OP_NIL, outReg, line, column);
    return {outReg, true};
}
ExprResult Compiler::compileBorrowExpr(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Borrow expression compilation is not implemented yet.");
    uint8_t outReg = regAlloc.allocate();
    emitOp(vm::OPCODE::OP_NIL, outReg, line, column);
    return {outReg, true};
}
ExprResult Compiler::compileImplicitCastExpr(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Implicit cast compilation is not implemented yet.");
    uint8_t outReg = regAlloc.allocate();
    emitOp(vm::OPCODE::OP_NIL, outReg, line, column);
    return {outReg, true};
}

} // namespace niki::syntax
