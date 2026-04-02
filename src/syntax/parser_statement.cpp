#include "niki/syntax/ast.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/parser_precedence.hpp"
#include "niki/syntax/token.hpp"
#include <cmath>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace niki::syntax;

// 语句解析的分发入口，类似于 parseDeclaration 的逻辑
ASTNodeIndex Parser::parseStatement() {
    if (match(TokenType::SYM_BRACE_L)) {
        return parseBlockStmt();
    } else if (match(TokenType::KEYWORD_IF)) {
        return parseIfStmt();
    } else if (match(TokenType::KEYWORD_LOOP)) {
        return parseLoopStmt();
    } else if (match(TokenType::KEYWORD_MATCH)) {
        return parseMatchStmt();
    } else if (match(TokenType::KEYWORD_CONTINUE)) {
        return parseContinueStmt();
    } else if (match(TokenType::KEYWORD_BREAK)) {
        return parseBreakStmt();
    } else if (match(TokenType::KEYWORD_RETURN)) {
        return parseReturnStmt();
    } else if (match(TokenType::NK_FLOW_NOCK)) {
        return parseNockStmt();
    }
    // ... 其他语句类型

    // 如果没有任何控制流关键字，则默认解析为表达式语句
    return parseExpressionStmt();
}

ASTNodeIndex Parser::parseExpressionStmt() {
    Token startToken = current;

    ASTNodeIndex expr = parseExpression(Precedence::None);
    // 提前拦截赋值语句，将之变形为表达式。
    // 注意，我们在match方法中包含了一个advance方法，
    // 假设我们传入的是一个a =1+2;的表达式,图表如下。

    /*p= previous, c=current
      p↓c↓
      +↓+↓+-+-+-+-+---
      |a|=|1|+|2|;|...
      +-+-+-+-+-+-+---
      在match之后，我们的p和c向前挪移一位。
        p↓c↓
      +-+↓+↓+-+-+-+---
      |a|=|1|+|2|;|...
      +-+-+-+-+-+-+---
      因此，我们应记录的OP应以previous而非current。
      之所以这样做，是因为，我们的一些局部解析当中，需要“往前看一位”而不破坏整体指针位置。
    */
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
ASTNodeIndex Parser::parseVarDeclStmt() {
    Token startToken = previous;
    ASTNodePayload payload{};
    consume(TokenType::IDENTIFIER, "Expected'value name'after'var'.");
    std::string_view str = source.substr(previous.start_offset, previous.length);
    payload.var_decl.name_id = astPool.internString(str);
    if (match(TokenType::SYM_COLON)) {
        payload.var_decl.type_expr = parseExpression(Precedence::None);
    } else {
        payload.var_decl.type_expr = ASTNodeIndex::invalid();
    }
    consume(TokenType::SYM_EQUAL, "Expected'='after 'var'.");
    payload.var_decl.init_expr = parseExpression(Precedence::None);

    consume(TokenType::SYM_SEMICOLON, "Expected';'after expression.");

    return emitNode(NodeType::VarDeclStmt, payload, startToken);
};
// 我们使用vardeclpayload来承载const——因为它们是一样的
ASTNodeIndex Parser::parseConstDeclStmt() {
    Token startToken = previous;
    ASTNodePayload payload{};
    consume(TokenType::IDENTIFIER, "Expected 'constant name' after 'const'.");
    std::string_view str = source.substr(previous.start_offset, previous.length);
    payload.var_decl.name_id = astPool.internString(str);
    if (match(TokenType::SYM_COLON)) {
        payload.var_decl.type_expr = parseExpression(Precedence::None);
    } else {
        payload.var_decl.type_expr = ASTNodeIndex::invalid();
    }
    consume(TokenType::SYM_EQUAL, "Expected '=' after constant declaration.");
    payload.var_decl.init_expr = parseExpression(Precedence::None);

    consume(TokenType::SYM_SEMICOLON, "Expected ';' after constant declaration.");

    return emitNode(NodeType::ConstDeclStmt, payload, startToken);
};

ASTNodeIndex Parser::parseBlockStmt() {
    Token startToken = previous;
    ASTNodePayload payload{};
    std::vector<ASTNodeIndex> statements;
    while (!check(TokenType::SYM_BRACE_R) && !check(TokenType::TOKEN_EOF)) {
        statements.push_back(parseDeclaration());
    }

    consume(TokenType::SYM_BRACE_R, "Expected '}' after block.");

    payload.block.statements = astPool.allocateList(statements);
    return emitNode(NodeType::BlockStmt, payload, startToken);
};
//---控制流
ASTNodeIndex Parser::parseIfStmt() {
    Token startToken = previous;

    ASTNodePayload IfStmtPayload{};

    consume(TokenType::SYM_PAREN_L, "Expected'('after 'if'.");

    IfStmtPayload.if_stmt.condition = parseExpression(Precedence::None);

    consume(TokenType::SYM_PAREN_R, "Expected')'after 'if condition'.");

    IfStmtPayload.if_stmt.then_branch = parseStatement();

    if (match(TokenType::KEYWORD_ELSE)) {
        if (match(TokenType::KEYWORD_IF)) {
            IfStmtPayload.if_stmt.else_branch = parseIfStmt();
        } else {
            IfStmtPayload.if_stmt.else_branch = parseStatement();
        }
    }

    return emitNode(NodeType::IfStmt, IfStmtPayload, startToken);
}
ASTNodeIndex Parser::parseLoopStmt() {};
ASTNodeIndex Parser::parseMatchStmt() {};
ASTNodeIndex Parser::parseMatchCaseStmt() {};
//---跳转中断---
ASTNodeIndex Parser::parseContinueStmt() {
    Token startToken = previous;
    ASTNodePayload payload{};
    consume(TokenType::SYM_COLON, "Expected ';' after 'continue'.");
    return emitNode(NodeType::ContinueStmt, payload, startToken);
};
ASTNodeIndex Parser::parseBreakStmt() {
    Token startToken = previous;
    ASTNodePayload payload{}; // 零负载！
    consume(TokenType::SYM_SEMICOLON, "Expected ';' after 'break'.");
    return emitNode(NodeType::BreakStmt, payload, startToken);
};
ASTNodeIndex Parser::parseReturnStmt() {};
ASTNodeIndex Parser::parseNockStmt() {};
//---组件挂载与卸载---
ASTNodeIndex Parser::parseAttachStmt() {};
ASTNodeIndex Parser::parseDetachStmt() {};
ASTNodeIndex Parser::parseTargetStmt() {};
ASTNodeIndex Parser::parseThrowStmt() {};
ASTNodeIndex Parser::parseTryCatchStmt() {};
