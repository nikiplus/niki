#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"
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
void Compiler::compileIfStmt(ASTNodeIndex nodeIdx) {}
void Compiler::compileLoopStmt(ASTNodeIndex nodeIdx) {}
void Compiler::compileMatchStmt(ASTNodeIndex nodeIdx) {}
void Compiler::compileMatchCase(ASTNodeIndex nodeIdx) {}

// 跳转与中断
void Compiler::compileContinueStmt(ASTNodeIndex nodeIdx) {}
void Compiler::compileBreakStmt(ASTNodeIndex nodeIdx) {}
void Compiler::compileReturnStmt(ASTNodeIndex nodeIdx) {}
void Compiler::compileNockStmt(ASTNodeIndex nodeIdx) {}

// 组件挂载与卸载
void Compiler::compileAttachStmt(ASTNodeIndex nodeIdx) {}
void Compiler::compileDetachStmt(ASTNodeIndex nodeIdx) {}
void Compiler::compileTargetStmt(ASTNodeIndex nodeIdx) {}

// 异常处理
void Compiler::compileThrowStmt(ASTNodeIndex nodeIdx) {}
void Compiler::compileTryCatchStmt(ASTNodeIndex nodeIdx) {}

} // namespace niki::syntax