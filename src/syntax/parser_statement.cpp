#include "niki/syntax/ast.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/token.hpp"
#include <cmath>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

using namespace niki::syntax;

ASTNodeIndex Parser::parseStatement() {};

ASTNodeIndex Parser::parseExpressionStmt() {
    Token startToken = current;

    ASTNodeIndex expr = parseExpression(Precedence::None);
    // 提前拦截赋值语句，将之变形为表达式。
    if (match(TokenType::SYM_EQUAL) || match(TokenType::SYM_BIT_AND_EQUAL) || match(TokenType::SYM_BIT_OR_EQUAL) ||
        match(TokenType::SYM_BIT_XOR_EQUAL) || match(TokenType::SYM_PLUS_EQUAL) || match(TokenType::SYM_MINUS_EQUAL) ||
        match(TokenType::SYM_STAR_EQUAL) || match(TokenType::SYM_SLASH_EQUAL) || match(TokenType::SYM_MOD_EQUAL) ||
        match(TokenType::SYM_SLASH_EQUAL) || match(TokenType::SYM_MOD_EQUAL)) {
        TokenType assignOp = previous.type;

        ASTNodePayload assignPayload{};
        assignPayload.assign_stmt.op = assignOp;
        assignPayload.assign_stmt.target = expr;

        assignPayload.assign_stmt.value = parseExpression(Precedence::None);

        consume(TokenType::SYM_SEMICOLON, "Expected';'after assignment.");
        return emitNode(NodeType::AssignmentStmt, assignPayload, startToken);
    }
    consume(TokenType::SYM_SEMICOLON, "Expected';'after expression.");
    ASTNodePayload exprPayload{};
    exprPayload.expr_stmt.expression = expr;
    return emitNode(NodeType::ExpressionStmt, exprPayload, startToken);
};
// parseAssignmentStmt无需存在，因为我们已在parseExpressionStmt中完成了对赋值语句的拦截。
// 事实上，由于赋值语句没有专门的开头关键字，因此我们实际上也是无法仅通过开头关键字判断一段字符是否是赋值语句，只有当左值token被解析完毕后，来到第二个toke，我们看到其为任意赋值语句时
// 才能将其作为赋值语句返回。
ASTNodeIndex Parser::parseVarDeclStmt() {};
ASTNodeIndex Parser::parseConstDeclStmt() {};
ASTNodeIndex Parser::parseBlockStmt() {};
//---控制流
ASTNodeIndex Parser::parseIfStmt() {};
ASTNodeIndex Parser::parseLoopStmt() {};
ASTNodeIndex Parser::parseMatchStmt() {};
ASTNodeIndex Parser::parseMatchCaseStmt() {};
//---跳转中断---
ASTNodeIndex Parser::parseContinueStmt() {};
ASTNodeIndex Parser::parseBreakStmt() {};
ASTNodeIndex Parser::parseReturnStmt() {};
ASTNodeIndex Parser::parseNockStmt() {};
//---组件挂载与卸载---
ASTNodeIndex Parser::parseAttachStmt() {};
ASTNodeIndex Parser::parseDetachStmt() {};
ASTNodeIndex Parser::parseTargetStmt() {};
ASTNodeIndex Parser::parseThrowStmt() {};
ASTNodeIndex Parser::parseTryCatchStmt() {};
