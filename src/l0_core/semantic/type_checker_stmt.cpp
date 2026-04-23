#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/semantic/type_checker.hpp"
#include <string>

namespace niki::semantic {

void TypeChecker::checkStatement(syntax::ASTNodeIndex stmtIdx) {
    // 语句检查入口：路由到对应 checkXXXStmt。
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
    auto &node = getNodeCtx(nodeIdx).node;
    checkExpression(node.payload.expr_stmt.expression);
}

void TypeChecker::checkAssignmentStmt(syntax::ASTNodeIndex nodeIdx) {
    // 赋值检查：左右类型均已知且不相等时报错。
    auto [node, line, column] = getNodeCtx(nodeIdx);
    NKType targetType = checkExpression(node.payload.assign_stmt.target);
    NKType valueType = checkExpression(node.payload.assign_stmt.value);
    if (targetType.getBase() != semantic::NKBaseType::Unknown && valueType.getBase() != semantic::NKBaseType::Unknown) {
        if (targetType != valueType) {
            reportError(line, column, "Type mismatch in assignment statement.");
        }
    }
}

void TypeChecker::checkVarDeclStmt(syntax::ASTNodeIndex nodeIdx) {
    // 变量声明：解析标注/初始化类型，校验一致性并注册最终符号类型。
    auto [node, line, column] = getNodeCtx(nodeIdx);

    NKType declType = NKType::makeUnknown();
    if (node.payload.var_decl.type_expr.isvalid()) {
        declType = resolveTypeAnnotation(node.payload.var_decl.type_expr);
    }

    NKType initType = NKType::makeUnknown();
    if (node.payload.var_decl.init_expr.isvalid()) {
        initType = checkExpression(node.payload.var_decl.init_expr);
    }
    if (declType.getBase() != semantic::NKBaseType::Unknown && initType.getBase() != semantic::NKBaseType::Unknown) {
        if (declType != initType) {
            reportError(line, column, "Type mismatch in varibale declaration.");
        }
    }
    NKType finalType = declType.getBase() != semantic::NKBaseType::Unknown ? declType : initType;

    if (finalType.getBase() == semantic::NKBaseType::Unknown) {
        reportError(line, column, "Cannot infer type for variable.Type annotation or initializer required.");
    }
    declareSymbol(node.payload.var_decl.name_id, finalType, line, column);
}

void TypeChecker::checkBlockStmt(syntax::ASTNodeIndex nodeIdx) {
    // 块作用域：beginScope -> statements -> endScope。
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

    // 2. return 只能出现在函数体内
    if (!inFunction) {
        reportError(line, column, "Cannot return from outside a function.");
        return;
    }
    // 3. 显式标注了返回类型时才做严格比对；未标注时允许推导（当前阶段不强制一致性）。
    if (currentReturnType.getBase() != NKBaseType::Unknown && exprType.getBase() != NKBaseType::Unknown &&
        exprType != currentReturnType) {
        reportError(line, column,
                    "Return type mismatch. Expected " + std::to_string((int)currentReturnType.getBase()) + ", got " +
                        std::to_string((int)exprType.getBase()));
    }
}
void TypeChecker::checkNockStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkAttachStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkDetachStmt(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkTargetStmt(syntax::ASTNodeIndex nodeIdx) {}

} // namespace niki::semantic