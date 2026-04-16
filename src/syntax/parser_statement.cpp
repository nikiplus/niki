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
    } else if (match(TokenType::KW_IF)) {
        return parseIfStmt();
    } else if (match(TokenType::KW_LOOP)) {
        return parseLoopStmt();
    } else if (match(TokenType::KW_MATCH)) {
        return parseMatchStmt();
    } else if (match(TokenType::KW_CONTINUE)) {
        return parseContinueStmt();
    } else if (match(TokenType::KW_BREAK)) {
        return parseBreakStmt();
    } else if (match(TokenType::KW_RETURN)) {
        return parseReturnStmt();
    } else if (match(TokenType::KW_NOCK)) {
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
        match(TokenType::SYM_STAR_EQUAL) || match(TokenType::SYM_SLASH_EQUAL) || match(TokenType::SYM_MOD_EQUAL)) {
        TokenType assignOp = previous.type;

        ASTNodePayload assignPayload{};
        assignPayload.assign_stmt.op = assignOp;
        assignPayload.assign_stmt.target = expr;

        assignPayload.assign_stmt.value = parseExpression(Precedence::None);

        consume(TokenType::SYM_SEMICOLON, "Expected ';' after assignment.");
        return emitNode(NodeType::AssignmentStmt, assignPayload, startToken);
    }
    consume(TokenType::SYM_SEMICOLON, "Expected ';' after expression.");
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

    if (match(TokenType::SYM_EQUAL)) {
        payload.var_decl.init_expr = parseExpression(Precedence::None);
    } else {
        payload.var_decl.init_expr = ASTNodeIndex::invalid();
    }

    consume(TokenType::SYM_SEMICOLON, "Expected ';' after variable declaration.");

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

    payload.list.elements = astPool.allocateList(statements);
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

    if (match(TokenType::KW_ELSE)) {
        if (match(TokenType::KW_IF)) {
            IfStmtPayload.if_stmt.else_branch = parseIfStmt();
        } else {
            IfStmtPayload.if_stmt.else_branch = parseStatement();
        }
    }

    return emitNode(NodeType::IfStmt, IfStmtPayload, startToken);
}
ASTNodeIndex Parser::parseLoopStmt() {
    Token startToken = previous;
    ASTNodePayload payload{};

    // 1. 可选的条件表达式 (condition)
    if (match(TokenType::SYM_PAREN_L)) {
        payload.loop.condition = parseExpression(Precedence::None);
        consume(TokenType::SYM_PAREN_R, "Expected ')' after loop condition.");
    } else {
        // 如果没有条件，说明是无限循环，用 invalid() 标记为空
        payload.loop.condition = ASTNodeIndex::invalid();
    }

    // 2. 强制要求代码块
    // 我们不手动吃掉 '{'，而是通过 check() 探视一下，确保接下来的是个代码块。
    if (!check(TokenType::SYM_BRACE_L)) {
        errorAtCurrent("Expected '{' after 'loop'. Loop body must be a block.");
        return emitNode(NodeType::ErrorNode, payload);
    }

    // 然后放心地把控制权交给 parseStatement，它内部遇到 '{' 自然会路由到 parseBlockStmt
    payload.loop.body = parseStatement();

    return emitNode(NodeType::LoopStmt, payload, startToken);
}

ASTNodeIndex Parser::parseMatchCaseStmt() {
    Token startToken = previous;
    ASTNodePayload payload{};
    std::vector<ASTNodeIndex> patterns;
    do {
        if (match(TokenType::KW_WILDCARD)) {
            ASTNodePayload wildcard_payload{};
            patterns.push_back(emitNode(NodeType::WildcardExpr, wildcard_payload, startToken));
        } else {
            patterns.push_back(parseExpression(Precedence::None));
        }
    } while (match(TokenType::SYM_COMMA));

    consume(TokenType::SYM_FAT_ARROW, "Expected'=>'after match patterns");
    payload.match_case.pattern = astPool.allocateList(patterns);

    payload.match_case.body = parseStatement();

    return emitNode(NodeType::MatchCaseStmt, payload, startToken);
}

ASTNodeIndex Parser::parseMatchStmt() {
    Token startToken = previous;
    ASTNodePayload payload{};

    consume(TokenType::SYM_PAREN_L, "Expected '(' after 'match'.");
    payload.match_stmt.expression = parseExpression(Precedence::None);
    consume(TokenType::SYM_PAREN_R, "Expected ')' after 'match target.'");

    consume(TokenType::SYM_BRACE_L, "Expected '{' before match cases.");

    std::vector<ASTNodeIndex> cases;
    while (!check(TokenType::SYM_BRACE_R) && !isAtEnd(TokenType::TOKEN_EOF)) {
        if (match(TokenType::KW_CASE)) {
            cases.push_back(parseMatchCaseStmt());
        } else {
            errorAtCurrent("Expected 'case' keyword inside match block.");

            synchronize();
        }
    }
    consume(TokenType::SYM_BRACE_R, "Expected '}' after match cases.");
    payload.match_stmt.cases = astPool.allocateList(cases);
    return emitNode(NodeType::MatchStmt, payload, startToken);
}
//---跳转中断---
ASTNodeIndex Parser::parseContinueStmt() {
    Token startToken = previous;
    ASTNodePayload payload{};
    consume(TokenType::SYM_SEMICOLON, "Expected ';' after 'continue'.");
    return emitNode(NodeType::ContinueStmt, payload, startToken);
};
ASTNodeIndex Parser::parseBreakStmt() {
    Token startToken = previous;
    ASTNodePayload payload{}; // 零负载！
    consume(TokenType::SYM_SEMICOLON, "Expected ';' after 'break'.");
    return emitNode(NodeType::BreakStmt, payload, startToken);
};
ASTNodeIndex Parser::parseReturnStmt() {
    Token startToken = previous;
    ASTNodePayload payload{};

    if (check(TokenType::SYM_SEMICOLON)) {
        payload.return_stmt.expression = ASTNodeIndex::invalid();
    } else {
        payload.return_stmt.expression = parseExpression(Precedence::None);
    }

    consume(TokenType::SYM_SEMICOLON, "Expected ';' after 'return'.");
    return emitNode(NodeType::ReturnStmt, payload, startToken);
}
ASTNodeIndex Parser::parseNockStmt() {
    Token startToken = previous;
    ASTNodePayload payload{};

    // 如果紧跟着分号，代表无条件让出当前帧 (nock;)
    if (check(TokenType::SYM_SEMICOLON)) {
        payload.nock.interval = ASTNodeIndex::invalid();
    } else {
        // 否则解析时间间隔表达式 (nock 5;)
        payload.nock.interval = parseExpression(Precedence::None);
    }

    consume(TokenType::SYM_SEMICOLON, "Expected ';' after 'nock'.");
    return emitNode(NodeType::NockStmt, payload, startToken);
}
//---组件挂载与卸载---
ASTNodeIndex Parser::parseAttachStmt() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseDetachStmt() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseTargetStmt() { return ASTNodeIndex{}; }
