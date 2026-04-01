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
    Parser(std::string_view source, std::span<const Token> tokens, ASTPool &pool);
    ASTNodeIndex parse();

  private:
    std::string_view source; // 获取字节源
    std::span<const Token> tokens;
    size_t tokenIndex = 0;
    ASTPool &astPool;

    Token current;
    Token previous;
    // hadError:单向的全局污染标记，一旦其被设定为ture，在整个编译生命周期内，它永远不可能再变回false。
    // 它的出现意味着，在这次AST生成过程中，我们的解析器跳过了错误，导致这颗ast是残缺的，其绝不能进入下一步解析
    bool hadError = false;
    // panicMode:可控的局部状态开关，当解析器遇到一个语法错误时，panicMode=ture，直到解析器通过synchronize()函数找到下一个同步点（如分号），才将之设定为false。
    // 这是为了防止连环报错，因为当我们在该行第一次扫描到不和法token时，其后续token极大概率也是不合法的，这时我们已无必要再向控制台打印无用信息了，只需跳过当前句段即可。
    bool panicMode = false;

    //---Token游标控制---
    void advance();
    void consume(TokenType type, const char *message);
    bool check(TokenType type) const;
    bool match(TokenType type);

    //---AST节点生成器---
    // 这里我们传入一个空token，用来进行长函数定位
    ASTNodeIndex emitNode(NodeType type, const ASTNodePayload &payload,
                          Token startToken = Token(0, 0, 0, 0, TokenType::TOKEN_EOF));

    //---错误处理---
    void errorAtCurrent(const char *message); // 处理parse阶段遇到的错误信息
    void synchronize();

    // 下面的各类解析方法我就不在这里做介绍和解释了，我们到实际的实现里面再讨论。
    // 为了防止单个文件内有过多代码行数，我们将实现以“主实现”，”顶层声明”，“语句”和“表达式”进行分类↓
    // 分别对应parser.cpp|parser_declaration.cpp|parser_statement.cpp|parser_expression.cpp四个实现文件。
    //---Pratt 表达式解析引擎---
    ASTNodeIndex parseExpression(Precedence precedence);
    ASTNodeIndex parsePrefix(TokenType type);
    ASTNodeIndex parseInfix(TokenType type, ASTNodeIndex left);
    Precedence getPrecedence(TokenType type) const;

    //---顶层声明解析 (parser_declaration.cpp) ---
    ASTNodeIndex parseDeclaration();
    ASTNodeIndex parseFunctionDecl();
    ASTNodeIndex parseStructDecl();
    ASTNodeIndex parseEnumDecl();
    ASTNodeIndex parseTypeAliasDecl();
    ASTNodeIndex parseInterfaceDecl();
    ASTNodeIndex parseModuleDecl();
    ASTNodeIndex parseSystemDecl();
    ASTNodeIndex parseComponentDecl();
    ASTNodeIndex parseFlowDecl();
    ASTNodeIndex parseContextDecl();
    ASTNodeIndex parseTagDecl();
    ASTNodeIndex parseTagGroupDecl();

    //---语句解析 (parser_statement.cpp) ---
    ASTNodeIndex parseStatement();
    ASTNodeIndex parseExpressionStmt();
    ASTNodeIndex parseAssignmentStmt();
    ASTNodeIndex parseVarDeclStmt();
    ASTNodeIndex parseConstDeclStmt();
    ASTNodeIndex parseBlockStmt();
    ASTNodeIndex parseIfStmt();
    ASTNodeIndex parseLoopStmt();
    ASTNodeIndex parseMatchStmt();
    ASTNodeIndex parseMatchCaseStmt();
    ASTNodeIndex parseContinueStmt();
    ASTNodeIndex parseBreakStmt();
    ASTNodeIndex parseReturnStmt();
    ASTNodeIndex parseNockStmt();
    ASTNodeIndex parseAttachStmt();
    ASTNodeIndex parseDetachStmt();
    ASTNodeIndex parseTargetStmt();
    ASTNodeIndex parseThrowStmt();
    ASTNodeIndex parseTryCatchStmt();
};
} // namespace niki::syntax