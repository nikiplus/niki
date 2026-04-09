#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"
#include "niki/vm/opcode.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

namespace niki::syntax {

//---语句编译---
void Compiler::compileStatement(ASTNodeIndex stmtIdx) {
    if (!stmtIdx.isvalid()) {
        return;
    }
    const ASTNode &node = currentPool->getNode(stmtIdx);
    uint32_t line = currentPool->locations[stmtIdx.index].line;
    uint32_t column = currentPool->locations[stmtIdx.index].column;
    switch (node.type) {
    case NodeType::ExpressionStmt:
        compileExpressionStmt(stmtIdx);
        break;
    case NodeType::AssignmentStmt:
        compileAssignmentStmt(stmtIdx);
        break;
    case NodeType::VarDeclStmt:
        compileVarDeclStmt(stmtIdx);
        break;
    case NodeType::ConstDeclStmt:
        compileConstDeclStmt(stmtIdx);
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
    case NodeType::BreakStmt:
        compileBreakStmt(stmtIdx);
        break;
    case NodeType::ReturnStmt:
        compileReturnStmt(stmtIdx);
        break;
    // ... 可以根据需要补充更多case
    default:
        reportError(line, column, "Expected a statement.");
        break;
    }
}

// 基础语句
void Compiler::compileExpressionStmt(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    ExprResult resultReg = compileExpression(node.payload.expr_stmt.expression);
    freeIfTemp(resultReg);
}

void Compiler::compileAssignmentStmt(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;
    ASTNodeIndex targetIdx = node.payload.assign_stmt.target;
    if (!targetIdx.isvalid()) {
        return;
    }

    const ASTNode &targetNode = currentPool->getNode(targetIdx);
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
            // ... 可以继续扩展位运算等复合赋值 ...
            default:
                reportError(line, column, "Unsupported assignment operator.");
                break;
            }
            freeIfTemp(rightRes);
        }
    } else {
        reportError(line, column, "Invalid assignment target. Only simple variables are supported currently.");
    }
}

void Compiler::compileVarDeclStmt(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;
    uint32_t name_id = node.payload.var_decl.name_id;
    // 只在当前作用域深度查找是否重复声明！
    for (int i = (int)locals.size() - 1; i >= 0; i--) {
        // 如果已经退到了外层作用域，就不用查了，因为允许 Shadowing
        if (locals[i].depth != scopeDepth)
            break;

        if (locals[i].name_id == name_id) {
            reportError(line, column, "Variable already declared in this scope.");
            break; // 报错后跳出
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

void Compiler::compileConstDeclStmt(ASTNodeIndex nodeIdx) {}

void Compiler::compileBlockStmt(ASTNodeIndex stmtIdx) {
    const ASTNode &node = currentPool->getNode(stmtIdx);
    beginScope();

    std::span<const ASTNodeIndex> stmts = currentPool->get_list(node.payload.list.elements);
    for (ASTNodeIndex stmt : stmts) {
        compileStatement(stmt);
    }
    endScope();
}

// 控制流
void Compiler::compileIfStmt(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    ExprResult condRes = compileExpression(node.payload.if_stmt.condition);

    size_t jzPatchPos = emitJump(vm::OPCODE::OP_JZ, condRes.reg, line, column);

    freeIfTemp(condRes);

    compileStatement(node.payload.if_stmt.then_branch);

    if (node.payload.if_stmt.else_branch.isvalid()) {
        size_t jmpEndPatchPos = emitJump(vm::OPCODE::OP_JMP, line, column);

        patchJump(jzPatchPos, compilingChunk.code.size());

        compileStatement(node.payload.if_stmt.else_branch);
        patchJump(jmpEndPatchPos, compilingChunk.code.size());
    } else {
        patchJump(jzPatchPos, compilingChunk.code.size());
    }
}

void Compiler::compileLoopStmt(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

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

    loop_stack.pop_back(); // 修复泄漏：必须出栈，保证状态对称
}
void Compiler::compileMatchStmt(ASTNodeIndex nodeIdx) {}
void Compiler::compileMatchCaseStmt(ASTNodeIndex nodeIdx) {}
// 跳转与中断
void Compiler::compileContinueStmt(ASTNodeIndex nodeIdx) {
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;
    if (loop_stack.empty()) {
        reportError(line, column, "continue outside loop.");
        return;
    }
    // 目标地址已知，直接发射后向跳跃，拒绝冗余分配
    emitLoopBack(vm::OPCODE::OP_LOOP, loop_stack.back().start_pos, line, column);
}
void Compiler::compileBreakStmt(ASTNodeIndex nodeIdx) {
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;
    if (loop_stack.empty()) {
        reportError(line, column, "break outside loop.");
        return;
    }

    size_t patch = emitJump(vm::OPCODE::OP_JMP, line, column);
    loop_stack.back().break_patches.push_back(patch);
}
void Compiler::compileReturnStmt(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    if (node.payload.return_stmt.expression.isvalid()) {
        ExprResult res = compileExpression(node.payload.return_stmt.expression);
        // 将返回值搬运到 0 号寄存器，作为当前约定的返回值存储位置
        emitOp(vm::OPCODE::OP_MOVE, 0, res.reg, line, column);
        freeIfTemp(res);
    }

    emitOp(vm::OPCODE::OP_RETURN, line, column);
}
void Compiler::compileNockStmt(ASTNodeIndex nodeIdx) {}

// 组件挂载与卸载
void Compiler::compileAttachStmt(ASTNodeIndex nodeIdx) {}
void Compiler::compileDetachStmt(ASTNodeIndex nodeIdx) {}
void Compiler::compileTargetStmt(ASTNodeIndex nodeIdx) {}

// 异常处理

} // namespace niki::syntax