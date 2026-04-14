#include "niki/semantic/nktype.hpp"
#include "niki/semantic/type_checker.hpp"
#include <string>

namespace niki::semantic {

void TypeChecker::checkStatement(syntax::ASTNodeIndex stmtIdx) {
    const auto &node = getNodeCtx(stmtIdx).node;
    switch (node.type) {
    case syntax::NodeType::ExpressionStmt:
        checkExpressionStmt(stmtIdx);
        break;
    case syntax::NodeType::AssignmentStmt:
        checkAssignmentStmt(stmtIdx);
        break;
    case syntax::NodeType::VarDeclStmt:
        checkVarDeclStmt(stmtIdx);
        break;
    case syntax::NodeType::ConstDeclStmt:
        checkConstDeclStmt(stmtIdx);
        break;
    case syntax::NodeType::BlockStmt:
        checkBlockStmt(stmtIdx);
        break;
    case syntax::NodeType::IfStmt:
        checkIfStmt(stmtIdx);
        break;
    case syntax::NodeType::LoopStmt:
        checkLoopStmt(stmtIdx);
        break;
    case syntax::NodeType::MatchStmt:
        checkMatchStmt(stmtIdx);
        break;
    case syntax::NodeType::MatchCaseStmt:
        checkMatchCaseStmt(stmtIdx);
        break;
    case syntax::NodeType::ContinueStmt:
        checkContinueStmt(stmtIdx);
        break;
    case syntax::NodeType::BreakStmt:
        checkBreakStmt(stmtIdx);
        break;
    case syntax::NodeType::ReturnStmt:
        checkReturnStmt(stmtIdx);
        break;
    case syntax::NodeType::NockStmt:
        checkNockStmt(stmtIdx);
        break;
    case syntax::NodeType::AttachStmt:
        checkAttachStmt(stmtIdx);
        break;
    case syntax::NodeType::DetachStmt:
        checkDetachStmt(stmtIdx);
        break;
    case syntax::NodeType::TargetStmt:
        checkTargetStmt(stmtIdx);
        break;
    default:
        break;
    }
}

void TypeChecker::checkExpressionStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = getNodeCtx(nodeIdx).node;
    checkExpression(node.payload.expr_stmt.expression);
}

void TypeChecker::checkAssignmentStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = getNodeCtx(nodeIdx).node;
    checkExpression(node.payload.assign_stmt.target);
    checkExpression(node.payload.assign_stmt.value);
}

void TypeChecker::checkVarDeclStmt(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);

    NKType initType = NKType::makeUnknown();
    if (node.payload.var_decl.init_expr.isvalid()) {
        initType = checkExpression(node.payload.var_decl.init_expr);
    }
    declareSymbol(node.payload.var_decl.name_id, initType, line, column);
}

void TypeChecker::checkBlockStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = getNodeCtx(nodeIdx).node;
    beginScope(); // 进门加锁
    auto stmts = currentPool->get_list(node.payload.list.elements);
    for (auto stmt : stmts) {
        checkNode(stmt);
    }
    endScope(); // 出门解锁
}

void TypeChecker::checkIfStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = getNodeCtx(nodeIdx).node;
    checkExpression(node.payload.if_stmt.condition);
    checkStatement(node.payload.if_stmt.then_branch);
    if (node.payload.if_stmt.else_branch.isvalid()) {
        checkStatement(node.payload.if_stmt.else_branch);
    }
}

void TypeChecker::checkConstDeclStmt(syntax::ASTNodeIndex nodeIdx) { checkVarDeclStmt(nodeIdx); }
void TypeChecker::checkLoopStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkMatchStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkMatchCaseStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkContinueStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkBreakStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkReturnStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    // 1. 检查有没有表达式（比如 `return;` 还是 `return 10;`）
    NKType exprType = NKType(NKBaseType::Void, -1);

    if (node.payload.return_stmt.expression.isvalid()) {
        exprType = checkExpression(node.payload.return_stmt.expression);
    }

    // 2. 如果我们不在函数里（这不应该发生，但防御性编程要做好）或者允许返回任何类型
    if (currentReturnType.getBase() == NKBaseType::Unknown) {
        return; // MVP兜底放过
    }
    // 3. 严格比对：实际返回的类型 vs 函数签名期待的类型
    // 在 MVP 中，我们如果看到 exprType 是 Unknown，我们也选择放过（因为可能有未实现的表达式检查返回了 Unknown）
    if (exprType.getBase() != NKBaseType::Unknown && exprType != currentReturnType) {
        reportError(line, column,
                    "Returen type mismatch.Expected" + std::to_string((int)currentReturnType.getBase()) + ", got" +
                        std::to_string((int)exprType.getBase()));
    }
}
void TypeChecker::checkNockStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkAttachStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkDetachStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkTargetStmt(syntax::ASTNodeIndex nodeIdx) {}

} // namespace niki::semantic