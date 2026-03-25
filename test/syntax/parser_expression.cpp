#include "niki/syntax/parser.hpp"
using namespace niki::syntax;

ASTNodeIndex Parser::parseExpression(Precedence precedence) {
    ASTNodeIndex left = parsePrefix(previous.type);
    while (precedence < getPrecedence(current.type)) {
        advance();
        left = parseInfix(previous.type, left);
    }
    return left;
};

ASTNodeIndex Parser::parsePrefix(TokenType type) {

};

ASTNodeIndex Parser::parseInfix(TokenType type, ASTNodeIndex left) {};

Precedence Parser::getPrecedence(TokenType type) const {};