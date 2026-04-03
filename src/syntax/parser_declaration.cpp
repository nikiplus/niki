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
    } else if (match(TokenType::KEYWORD_TYPE)) {
        return parseTypeAliasDecl();
    } else if (match(TokenType::KEYWORD_INTERFACE)) {
        return parseInterfaceDecl();
    } else if (match(TokenType::KEYWORD_IMPL)) {
        return parseImplDecl();
    } else if (match(TokenType::NK_MODULE)) {
        return parseModuleDecl();
    } else if (match(TokenType::NK_SYSTEM)) {
        return parseSystemDecl();
    } else if (match(TokenType::NK_COMPONENT)) {
        return parseComponentDecl();
    } else if (match(TokenType::NK_FLOW)) {
        return parseFlowDecl();
    } else if (match(TokenType::NK_KITS)) {
        return parseKitsDecl();
    } else if (match(TokenType::NK_TAG)) {
        return parseTagDecl();
    } else if (match(TokenType::NK_TAGGROUP)) {
        return parseTagGroupDecl();
    }

    // 如果以上都不是，则它肯定是一个语句（或表达式语句）
    return parseStatement();
}

//---基础声明---
ASTNodeIndex Parser::parseFunctionDecl() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseStructDecl() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseEnumDecl() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseTypeAliasDecl() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseInterfaceDecl() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseImplDecl() { return ASTNodeIndex{}; }
//---NIKI特有---
ASTNodeIndex Parser::parseModuleDecl() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseSystemDecl() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseComponentDecl() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseFlowDecl() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseKitsDecl() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseTagDecl() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseTagGroupDecl() { return ASTNodeIndex{}; }
