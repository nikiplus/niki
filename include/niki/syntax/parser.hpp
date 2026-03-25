#pragma once

#include "ast.hpp"
#include "parser_precedence.hpp"
#include "token.hpp"
#include <span>
#include <string_view>

// 我们将解析器分为两部分实现，第一部分为表达式解析器，该部分我们使用普拉特解析算法，
//  第二部分为语句解析器，该部分我们使用递归下降算法。
// 在此之前，我们需要定义一个parser类，来链接表达式解析器和语句解析器。
namespace niki::syntax {
class Parser {
  public:
    // 严禁 Parser 自行分配内存，必须由调用方 (Compiler) 注入内存池
    Parser(std::span<const Token> tokens, ASTPool &pool);
    ASTNodeIndex parse();

  private:
    std::span<const Token> tokens;
    size_t tokenIndex = 0;
    ASTPool &astPool;

    Token current;
    Token previous;
    bool hadError = false;
    bool panicMode = false;

    //---Token游标控制---
    void advance();
    void consume(TokenType type, const char *message);
    bool check(TokenType type);
    bool match(TokenType type) const;

    //---AST节点生成器---
    ASTNodeIndex emitNode(NodeType type, const ASTNodePayload &payload);

    //---Pratt 表达式解析引擎---
    ASTNodeIndex parseExpression(Precedence precedence);
    ASTNodeIndex parsePrefix(TokenType type);
    ASTNodeIndex parseInfix(TokenType type, ASTNodeIndex left);
    Precedence getPrecedence(TokenType type) const;
};
} // namespace niki::syntax