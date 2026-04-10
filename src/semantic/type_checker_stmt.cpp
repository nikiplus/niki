#include "niki/semantic/type_checker.hpp"

namespace niki::semantic {

void TypeChecker::checkStatement(syntax::ASTNodeIndex stmtIdx) {
    const auto &node = currentPool->getNode(stmtIdx);
    switch (node.type) {
    case syntax::NodeType::ExpressionStmt:
        checkExpression(node.payload.expr_stmt.expression);
        break;
    case syntax::NodeType::VarDeclStmt:
        checkVarDeclStmt(stmtIdx);
        break;
    case syntax::NodeType::ConstDeclStmt:
        checkVarDeclStmt(stmtIdx);
        break; // 复用
    case syntax::NodeType::AssignmentStmt:
        checkAssignmentStmt(stmtIdx);
        break;
    case syntax::NodeType::BlockStmt:
        checkBlockStmt(stmtIdx);
        break;
    case syntax::NodeType::IfStmt:
        checkIfStmt(stmtIdx);
        break;
    default:
        break;
    }
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

} // namespace niki::semantic