#include "niki/syntax/ast.hpp"
#include "niki/syntax/parser.hpp"
using namespace niki::syntax;

// 顶层声明的解析入口，如果遇到声明关键字则进入对应的分支；
// 否则，它认为这是一个语句（Statement），将其降级给 parseStatement 处理。
ASTNodeIndex Parser::parseDeclaration() {
    if (match(TokenType::KEYWORD_VAR)) {
        return parseVarDeclStmt();
    } else if (match(TokenType::KEYWORD_CONST)) {
        return parseConstDeclStmt();
    } else if (match(TokenType::KEYWORD_FUNC)) {
        return parseFunctionDecl();
    } else if (match(TokenType::NK_STRUCT)) {
        return parseStructDecl();
    } else if (match(TokenType::NK_ENUM)) {
        return parseEnumDecl();
    }
    // ... 其他模块级声明的解析

    // 如果以上都不是，则它肯定是一个语句（或表达式语句）
    return parseStatement();
}

//---基础声明---
ASTNodeIndex Parser::parseFunctionDecl() {};
ASTNodeIndex Parser::parseStructDecl() {};
ASTNodeIndex Parser::parseEnumDecl() {};
ASTNodeIndex Parser::parseTypeAliasDecl() {};
ASTNodeIndex Parser::parseInterfaceDecl() {};
//---NIKI特有---
ASTNodeIndex Parser::parseModuleDecl() {};
ASTNodeIndex Parser::parseSystemDecl() {};
ASTNodeIndex Parser::parseComponentDecl() {};
ASTNodeIndex Parser::parseFlowDecl() {};
ASTNodeIndex Parser::parseKitsDecl() {};
ASTNodeIndex Parser::parseTagDecl() {};
ASTNodeIndex Parser::parseTagGroupDecl() {};
