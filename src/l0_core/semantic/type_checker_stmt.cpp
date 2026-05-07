#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/semantic/type_checker.hpp"
#include <string>

namespace niki::semantic {

/**
 * @brief 语句检查分发入口。
 * @param stmtIdx 语句节点索引。
 */
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
    default:
        break;
    }
}

/** @brief 检查表达式语句。 */
void TypeChecker::checkExpressionStmt(syntax::ASTNodeIndex nodeIdx) {
    auto &node = getNodeCtx(nodeIdx).node;
    checkExpression(node.payload.expr_stmt.expression);
}

/** @brief 检查赋值语句。 */
void TypeChecker::checkAssignmentStmt(syntax::ASTNodeIndex nodeIdx) {
    // 赋值检查：左右类型均已知且不相等时报错。
    auto [node, line, column] = getNodeCtx(nodeIdx);
    NKType targetType = checkExpression(node.payload.assign_stmt.target);
    NKType valueType = checkExpression(node.payload.assign_stmt.value);
    bool types_ok = true;
    if (targetType.getBase() != semantic::NKBaseType::Unknown && valueType.getBase() != semantic::NKBaseType::Unknown) {
        if (targetType != valueType) {
            reportError(line, column, "Type mismatch in assignment statement.",
                        niki::diagnostic::events::SemanticCode::TypeMismatch);
            types_ok = false;
        }
    }
    if (!types_ok) {
        return;
    }
    // 简单所有权转移：堆类型右值标识符在 `=` 赋值后视为已 move。
    if (node.payload.assign_stmt.op == syntax::TokenType::SYM_EQUAL) {
        const syntax::ASTNode &valNode = currentPool->getNode(node.payload.assign_stmt.value);
        if (valNode.type == syntax::NodeType::IdentifierExpr) {
            tryMarkRhsIdentifierMovedForAssign(valNode.payload.identifier.name_id);
        }
    }
}

/** @brief 检查变量声明语句。 */
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
    bool decl_init_type_clash = false;
    if (declType.getBase() != semantic::NKBaseType::Unknown && initType.getBase() != semantic::NKBaseType::Unknown) {
        if (declType != initType) {
            reportError(line, column, "Type mismatch in varibale declaration.",
                        niki::diagnostic::events::SemanticCode::TypeMismatch);
            decl_init_type_clash = true;
        }
    }
    NKType finalType = declType.getBase() != semantic::NKBaseType::Unknown ? declType : initType;

    if (finalType.getBase() == semantic::NKBaseType::Unknown) {
        reportError(line, column, "Cannot infer type for variable.Type annotation or initializer required.",
                    niki::diagnostic::events::SemanticCode::MissingTypeAnnotation);
    }
    // 初始化右值为堆类型拥有标识符时，视同 `=` 转移所有权（与 AssignmentStmt 一致）。
    if (!decl_init_type_clash && node.payload.var_decl.init_expr.isvalid()) {
        const syntax::ASTNode &initNode = currentPool->getNode(node.payload.var_decl.init_expr);
        if (initNode.type == syntax::NodeType::IdentifierExpr) {
            tryMarkRhsIdentifierMovedForAssign(initNode.payload.identifier.name_id);
        }
    }
    declareSymbol(node.payload.var_decl.name_id, finalType, line, column, isHeapType(finalType));
}

/** @brief 检查代码块语句并管理作用域。 */
void TypeChecker::checkBlockStmt(syntax::ASTNodeIndex nodeIdx) {
    // 块作用域：beginScope -> statements -> endScope。
    const auto &node = getNodeCtx(nodeIdx).node;
    beginScope(); // 进门加锁
    auto stmts = currentPool->get_list(node.payload.list.elements);
    for (auto stmt : stmts) {
        checkNode(stmt);
    }
    endBlockScope(nodeIdx); // 出门解锁并记录块尾待释放符号
}

/** @brief 检查 if 语句。 */
void TypeChecker::checkIfStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    const NKType condType = checkExpression(node.payload.if_stmt.condition);
    if (condType.getBase() != NKBaseType::Unknown && condType.getBase() != NKBaseType::Bool) {
        reportError(line, column, "If condition must be Bool.", niki::diagnostic::events::SemanticCode::NotABoolContext);
    }
    checkStatement(node.payload.if_stmt.then_branch);
    if (node.payload.if_stmt.else_branch.isvalid()) {
        checkStatement(node.payload.if_stmt.else_branch);
    }
}

/** @brief 检查常量声明语句（复用变量声明检查）。 */
void TypeChecker::checkConstDeclStmt(syntax::ASTNodeIndex nodeIdx) { checkVarDeclStmt(nodeIdx); }
/** @brief 检查 loop 语句：可选条件须为 Bool，并递归检查循环体。 */
void TypeChecker::checkLoopStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    if (node.payload.loop.condition.isvalid()) {
        const NKType condType = checkExpression(node.payload.loop.condition);
        if (condType.getBase() != NKBaseType::Unknown && condType.getBase() != NKBaseType::Bool) {
            reportError(line, column, "Loop condition must be Bool.",
                        niki::diagnostic::events::SemanticCode::NotABoolContext);
        }
    }
    ++loopNestingDepth;
    checkStatement(node.payload.loop.body);
    --loopNestingDepth;
}
/** @brief 检查 match 语句（占位实现）。 */
void TypeChecker::checkMatchStmt(syntax::ASTNodeIndex nodeIdx) {}
/** @brief 检查 match case 语句（占位实现）。 */
void TypeChecker::checkMatchCaseStmt(syntax::ASTNodeIndex nodeIdx) {}
/** @brief 检查 continue 语句：必须在循环体内。 */
void TypeChecker::checkContinueStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto ctx = getNodeCtx(nodeIdx);
    if (loopNestingDepth <= 0) {
        reportError(ctx.line, ctx.column, "continue used outside loop.",
                    niki::diagnostic::events::SemanticCode::GenericError);
    }
}
/** @brief 检查 break 语句：必须在循环体内。 */
void TypeChecker::checkBreakStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto ctx = getNodeCtx(nodeIdx);
    if (loopNestingDepth <= 0) {
        reportError(ctx.line, ctx.column, "break used outside loop.", niki::diagnostic::events::SemanticCode::GenericError);
    }
}
/** @brief 检查 return 语句。 */
void TypeChecker::checkReturnStmt(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    // 1. 检查有没有表达式（比如 `return;` 还是 `return 10;`）
    NKType exprType = NKType(NKBaseType::Void, -1);

    if (node.payload.return_stmt.expression.isvalid()) {
        exprType = checkExpression(node.payload.return_stmt.expression);
    }

    // 2. return 只能出现在函数体内
    if (!inFunction) {
        reportError(line, column, "Cannot return from outside a function.",
                    niki::diagnostic::events::SemanticCode::GenericError);
        return;
    }
    // 3. 显式标注了返回类型时才做严格比对；未标注时允许推导（当前阶段不强制一致性）。
    if (currentReturnType.getBase() != NKBaseType::Unknown && exprType.getBase() != NKBaseType::Unknown &&
        exprType != currentReturnType) {
        reportError(line, column,
                    "Return type mismatch. Expected " + std::to_string((int)currentReturnType.getBase()) + ", got " +
                        std::to_string((int)exprType.getBase()),
                    niki::diagnostic::events::SemanticCode::ReturnTypeMismatch);
    }
}
/** @brief 检查 nock 语句（占位实现）。 */
void TypeChecker::checkNockStmt(syntax::ASTNodeIndex nodeIdx) {}
/** @brief 检查 attach 语句（占位实现）。 */
void TypeChecker::checkAttachStmt(syntax::ASTNodeIndex nodeIdx) {}
/** @brief 检查 detach 语句（占位实现）。 */
void TypeChecker::checkDetachStmt(syntax::ASTNodeIndex nodeIdx) {}

} // namespace niki::semantic