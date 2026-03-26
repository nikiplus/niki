#include "niki/syntax/compiler.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/scanner.hpp"
#include "niki/syntax/token.hpp"
#include <iostream>
#include <span>

using namespace niki::syntax;
bool Compiler::compile(std::string_view source) {
    tokens.clear();
    tokens.reserve(source.length() / 4);
    Scanner s(source);
    for (;;) {
        Token token = s.scanToken();
        if (token.type == TokenType::COMMENT)
            continue;
        tokens.push_back(token);
        if (token.type == TokenType::TOKEN_ERROR)
            return false;

        if (token.type == TokenType::TOKEN_EOF)
            break;
    }
    Parser parser(source, tokens, astPool);
    rootNode = parser.parse();
    return rootNode.isvalid();
};