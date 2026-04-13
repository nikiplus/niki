#include "niki/semantic/type_checker.hpp"

namespace niki::semantic {

void TypeChecker::checkStatement(syntax::ASTNodeIndex stmtIdx) {
    const auto &node = currentPool->getNode(stmtIdx);
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
    const auto &node = currentPool->getNode(nodeIdx);
    checkExpression(node.payload.expr_stmt.expression);
}

void TypeChecker::checkVarDeclStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    NKType initType = NKType::makeUnknown();
    if (node.payload.var_decl.init_expr.isvalid()) {
        initType = checkExpression(node.payload.var_decl.init_expr);
    }
    declareSymbol(node.payload.var_decl.name_id, initType, line, column);
}

void TypeChecker::checkAssignmentStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = currentPool->getNode(nodeIdx);
    checkExpression(node.payload.assign_stmt.target);
    checkExpression(node.payload.assign_stmt.value);
}

void TypeChecker::checkBlockStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = currentPool->getNode(nodeIdx);
    beginScope();
    auto stmts = currentPool->get_list(node.payload.list.elements);
    for (auto stmt : stmts) {
        checkNode(stmt);
    }
    endScope();
}

void TypeChecker::checkIfStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = currentPool->getNode(nodeIdx);
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
void TypeChecker::checkReturnStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkNockStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkAttachStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkDetachStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkTargetStmt(syntax::ASTNodeIndex nodeIdx) {}

} // namespace niki::semantic