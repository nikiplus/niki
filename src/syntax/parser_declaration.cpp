#include "niki/syntax/ast.hpp"
#include "niki/syntax/parser.hpp"
using namespace niki::syntax;
// 还没写，后面有空再写，先把表达式解析跑通再说
ASTNodeIndex Parser::parseDeclaration() {};

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
ASTNodeIndex Parser::parseContextDecl() {};
ASTNodeIndex Parser::parseTagDecl() {};
ASTNodeIndex Parser::parseTagGroupDecl() {};
