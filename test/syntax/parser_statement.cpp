#include "niki/syntax/parser.hpp"

using namespace niki::syntax;

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
