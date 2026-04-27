#include "niki/debug/logger.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/compiler.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include "niki/l0_core/vm/opcode.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

namespace niki::syntax {

//---语句编译---
// 入口：分发语句节点到对应 compileXXXStmt。
void Compiler::compileStatement(ASTNodeIndex stmtIdx) {
    if (!stmtIdx.isvalid()) {
        return;
    }
    auto [node, line, column] = getNodeCtx(stmtIdx);
    niki::debug::trace("compiler", "{}|-> statement {} at {}:{}", makeTracePrefix(), toString(node.type), line, column);
    switch (node.type) {
    case NodeType::ExpressionStmt:
        compileExpressionStmt(stmtIdx);
        break;
    case NodeType::AssignmentStmt:
        compileAssignmentStmt(stmtIdx);
        break;
    case NodeType::BlockStmt:
        compileBlockStmt(stmtIdx);
        break;
    case NodeType::IfStmt:
        compileIfStmt(stmtIdx);
        break;
    case NodeType::LoopStmt:
        compileLoopStmt(stmtIdx);
        break;
    case NodeType::ContinueStmt:
        compileContinueStmt(stmtIdx);
        break;
    case NodeType::BreakStmt:
        compileBreakStmt(stmtIdx);
        break;
    case NodeType::ReturnStmt:
        compileReturnStmt(stmtIdx);
        break;
    case NodeType::VarDeclStmt:
        compileVarDeclStmt(stmtIdx);
        break;
    case NodeType::ConstDeclStmt:
        compileConstDeclStmt(stmtIdx);
        break;
    // ... 可以根据需要补充更多case
    default:
        reportError(line, column, "Expected a statement.");
        break;
    }
}

// 基础语句
void Compiler::compileExpressionStmt(ASTNodeIndex nodeIdx) {
    const auto &node = getNodeCtx(nodeIdx).node;
    ExprResult resultReg = compileExpression(node.payload.expr_stmt.expression);
    freeIfTemp(resultReg);
}

void Compiler::compileAssignmentStmt(ASTNodeIndex nodeIdx) {
    // 左值分流：标识符 / 索引 / 成员；复合赋值发射对应算术或位运算指令。
    auto [node, line, column] = getNodeCtx(nodeIdx);
    ASTNodeIndex targetIdx = node.payload.assign_stmt.target;
    if (!targetIdx.isvalid()) {
        return;
    }

    const auto &targetNode = getNodeCtx(targetIdx).node;
    if (targetNode.type == NodeType::IdentifierExpr) {
        uint32_t name_id = targetNode.payload.identifier.name_id;
        uint8_t targetReg = resolveLocal(name_id, line, column);
        TokenType op = node.payload.assign_stmt.op;

        if (op == TokenType::SYM_EQUAL) {
            ExprResult valRes = compileExpression(node.payload.assign_stmt.value);
            if (valRes.reg != targetReg) {
                emitOp(vm::OPCODE::OP_MOVE, targetReg, valRes.reg, line, column);
            }
            freeIfTemp(valRes);
        } else {
            ExprResult rightRes = compileExpression(node.payload.assign_stmt.value);

            switch (op) {
            case TokenType::SYM_PLUS_EQUAL:
                emitOp(vm::OPCODE::OP_IADD, targetReg, targetReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_MINUS_EQUAL:
                emitOp(vm::OPCODE::OP_ISUB, targetReg, targetReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_STAR_EQUAL:
                emitOp(vm::OPCODE::OP_IMUL, targetReg, targetReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_SLASH_EQUAL:
                emitOp(vm::OPCODE::OP_IDIV, targetReg, targetReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_MOD_EQUAL:
                emitOp(vm::OPCODE::OP_IMOD, targetReg, targetReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_BIT_AND_EQUAL:
                emitOp(vm::OPCODE::OP_BIT_AND, targetReg, targetReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_BIT_OR_EQUAL:
                emitOp(vm::OPCODE::OP_BIT_OR, targetReg, targetReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_BIT_XOR_EQUAL:
                emitOp(vm::OPCODE::OP_BIT_XOR, targetReg, targetReg, rightRes.reg, line, column);
                break;
            default:
                reportError(line, column, "Unsupported assignment operator.");
                break;
            }
            freeIfTemp(rightRes);
        }
    } else if (targetNode.type == NodeType::IndexExpr) {
        TokenType op = node.payload.assign_stmt.op;
        ExprResult targetRes = compileExpression(targetNode.payload.index.target);
        ExprResult indexRes = compileExpression(targetNode.payload.index.index);

        if (op == TokenType::SYM_EQUAL) {
            ExprResult valRes = compileExpression(node.payload.assign_stmt.value);
            emitOp(vm::OPCODE::OP_SET_ARRAY, targetRes.reg, indexRes.reg, valRes.reg, line, column);
            freeIfTemp(valRes);
        } else {
            ExprResult rightRes = compileExpression(node.payload.assign_stmt.value);
            uint8_t oldReg = regAlloc.allocate();
            uint8_t newReg = regAlloc.allocate();

            emitOp(vm::OPCODE::OP_GET_ARRAY, oldReg, targetRes.reg, indexRes.reg, line, column);

            bool supported = true;

            switch (op) {
            case TokenType::SYM_PLUS_EQUAL:
                emitOp(vm::OPCODE::OP_IADD, newReg, oldReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_MINUS_EQUAL:
                emitOp(vm::OPCODE::OP_ISUB, newReg, oldReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_STAR_EQUAL:
                emitOp(vm::OPCODE::OP_IMUL, newReg, oldReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_SLASH_EQUAL:
                emitOp(vm::OPCODE::OP_IDIV, newReg, oldReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_BIT_AND_EQUAL:
                emitOp(vm::OPCODE::OP_BIT_AND, newReg, oldReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_BIT_OR_EQUAL:
                emitOp(vm::OPCODE::OP_BIT_OR, newReg, oldReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_BIT_XOR_EQUAL:
                emitOp(vm::OPCODE::OP_BIT_XOR, newReg, oldReg, rightRes.reg, line, column);
                break;
            default:
                supported = false;
                reportError(line, column, "Unsupported assignment operator for indexed target.");
                break;
            }
            if (supported) {
                emitOp(vm::OPCODE::OP_SET_ARRAY, targetRes.reg, indexRes.reg, newReg, line, column);
            }
            freeIfTemp(rightRes);
            regAlloc.free(oldReg);
            regAlloc.free(newReg);
        }
        freeIfTemp(targetRes);
        freeIfTemp(indexRes);
    } else if (targetNode.type == NodeType::MemberExpr) {
        reportWarning(line, column, "Object property assignment is a stub.");
    } else {
        reportError(line, column, "Invalid assignment target. Only simple variables are supported currently.");
    }
}

void Compiler::compileVarDeclStmt(ASTNodeIndex nodeIdx) {
    // 作用域内查重后分配寄存器；有初始化则 MOVE，否则写 NIL。
    auto [node, line, column] = getNodeCtx(nodeIdx);
    uint32_t name_id = node.payload.var_decl.name_id;
    // 只在当前作用域深度查找是否重复声明！
    for (int i = (int)locals.size() - 1; i >= 0; i--) {
        // 如果已经退到了外层作用域，就不用查了，因为允许 Shadowing
        if (locals[i].depth != scopeDepth)
            break;

        if (locals[i].name_id == name_id) {
            reportError(line, column, "Variable already declared in this scope.");
            break; // 报错后跳转
        }
    }

    uint8_t varReg = regAlloc.allocate();
    locals.push_back({name_id, varReg, scopeDepth});

    if (node.payload.var_decl.init_expr.isvalid()) {
        ExprResult initRes = compileExpression(node.payload.var_decl.init_expr);
        if (initRes.reg != varReg) {
            emitOp(vm::OPCODE::OP_MOVE, varReg, initRes.reg, line, column);
        }
        freeIfTemp(initRes);
    } else {
        emitOp(vm::OPCODE::OP_NIL, varReg, line, column);
    }
}

void Compiler::compileConstDeclStmt(ASTNodeIndex nodeIdx) {
    // MVP 阶段：复用变量声明的逻辑（未来可以在语义检查期强制不可变性）
    compileVarDeclStmt(nodeIdx);
}

void Compiler::compileBlockStmt(ASTNodeIndex stmtIdx) {
    // 生命周期：beginScope -> statements -> endScope。
    auto [node, line, column] = getNodeCtx(stmtIdx);
    niki::debug::trace("compiler", "{}|-> enter BlockStmt at {}:{}", makeTracePrefix(), line, column);
    traceIndent++;
    beginScope();

    std::span<const ASTNodeIndex> stmts = currentPool->get_list(node.payload.list.elements);
    for (ASTNodeIndex stmt : stmts) {
        compileStatement(stmt);
    }
    endScope();
    if (traceIndent > 0) {
        traceIndent--;
    }
    niki::debug::trace("compiler", "{}|<- leave BlockStmt at {}:{}", makeTracePrefix(), line, column);
}

// 控制流
void Compiler::compileIfStmt(ASTNodeIndex nodeIdx) {
    // 模板：condition -> JZ 占位 -> then -> 可选 else -> patchJump。
    // 底层视角：
    // - 先发射占位跳转，再在目标地址确定后回填 offset（典型两遍式 patch）。
    // - 这对应机器码生成中“前向引用尚未知地址”的常见处理模式。
    auto [node, line, column] = getNodeCtx(nodeIdx);
    niki::debug::trace("compiler", "{}|-> if at {}:{}", makeTracePrefix(), line, column);

    ExprResult condRes = compileExpression(node.payload.if_stmt.condition);

    size_t jzPatchPos = emitJump(vm::OPCODE::OP_JZ, condRes.reg, line, column);

    freeIfTemp(condRes);

    niki::debug::trace("compiler", "{}|- then", makeTracePrefix());
    traceIndent++;
    compileStatement(node.payload.if_stmt.then_branch);
    if (traceIndent > 0) {
        traceIndent--;
    }

    if (node.payload.if_stmt.else_branch.isvalid()) {
        size_t jmpEndPatchPos = emitJump(vm::OPCODE::OP_JMP, line, column);

        patchJump(jzPatchPos, compilingChunk->code.size());

        niki::debug::trace("compiler", "{}`- else", makeTracePrefix());
        traceIndent++;
        compileStatement(node.payload.if_stmt.else_branch);
        if (traceIndent > 0) {
            traceIndent--;
        }
        patchJump(jmpEndPatchPos, compilingChunk->code.size());
    } else {
        patchJump(jzPatchPos, compilingChunk->code.size());
    }
}

void Compiler::compileLoopStmt(ASTNodeIndex nodeIdx) {
    // 模板：记录 loopStart，编译可选条件与循环体，回跳并回填 break 补丁。
    // 底层视角：
    // - continue 是已知目标（loopStart）的后向跳转；
    // - break 是未知目标（loopEnd）的前向跳转，先占位，出循环后统一 patch。
    auto [node, line, column] = getNodeCtx(nodeIdx);

    size_t loopStart = currentCodePos();
    size_t exitPatch = static_cast<size_t>(-1);
    loop_stack.push_back(LoopContext{loopStart, {}});

    if (node.payload.loop.condition.isvalid()) {
        ExprResult condRes = compileExpression(node.payload.loop.condition);

        exitPatch = emitJump(vm::OPCODE::OP_JZ, condRes.reg, line, column);
        freeIfTemp(condRes);
    }
    compileStatement(node.payload.loop.body);

    emitLoopBack(vm::OPCODE::OP_LOOP, loopStart, line, column);
    if (exitPatch != static_cast<size_t>(-1)) {
        patchJump(exitPatch, currentCodePos());
    }
    size_t loopEnd = currentCodePos();
    LoopContext &ctx = loop_stack.back();

    for (size_t p : ctx.break_patches) {
        patchJump(p, loopEnd);
    }

    loop_stack.pop_back(); // 修复泄漏：必须出栈，保证状态
}
void Compiler::compileMatchStmt(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Match statement compilation is not implemented yet.");
}
void Compiler::compileMatchCaseStmt(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Match case compilation is not implemented yet.");
}
// 跳转与中断
void Compiler::compileContinueStmt(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    if (loop_stack.empty()) {
        reportError(line, column, "continue outside loop.");
        return;
    }
    // 目标地址已知，直接发射后向跳跃，拒绝冗余分配。
    // 本质是“显式改写下一 PC 到循环头”，而非重复解析循环体源码。
    emitLoopBack(vm::OPCODE::OP_LOOP, loop_stack.back().start_pos, line, column);
}
void Compiler::compileBreakStmt(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    if (loop_stack.empty()) {
        reportError(line, column, "break outside loop.");
        return;
    }

    // break 不会“停止执行”，它是跳到循环出口块的控制流转移。
    // 由于出口地址尚未知，先发射占位跳转并记录 patch 点。
    size_t patch = emitJump(vm::OPCODE::OP_JMP, line, column);
    loop_stack.back().break_patches.push_back(patch);
}
void Compiler::compileReturnStmt(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);

    if (node.payload.return_stmt.expression.isvalid()) {
        ExprResult res = compileExpression(node.payload.return_stmt.expression);
        // 将返回值搬运到 0 号寄存器，作为当前约定的返回值存储位
        emitOp(vm::OPCODE::OP_MOVE, 0, res.reg, line, column);
        freeIfTemp(res);
    }

    emitOp(vm::OPCODE::OP_RETURN, line, column);
}
void Compiler::compileNockStmt(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Nock statement compilation is not implemented yet.");
}

// 组件挂载与卸载
void Compiler::compileAttachStmt(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Attach statement compilation is not implemented yet.");
}
void Compiler::compileDetachStmt(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Detach statement compilation is not implemented yet.");
}
void Compiler::compileTargetStmt(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Target statement compilation is not implemented yet.");
}

// 异常处理

} // namespace niki::syntax
