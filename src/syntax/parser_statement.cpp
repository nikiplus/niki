#include "niki/syntax/parser.hpp"
#include <span>

using namespace niki::syntax;
Parser::Parser(std::span<const Token> tokens, ASTPool &pool) : tokens(tokens), astPool(pool), tokenIndex(0) {
    advance();
}

//---游标控制---
void Parser::advance() {
    previous = current;             // 初始化previous指针
    current = tokens[tokenIndex++]; // 推进current指针
};

void Parser::consume(TokenType type, const char *message) {};
bool Parser::check(TokenType type) {};
bool Parser::match(TokenType type) const {};

//---AST节点生成器---
ASTNodeIndex Parser::emitNode(NodeType type, const ASTNodePayload &payload) {};
