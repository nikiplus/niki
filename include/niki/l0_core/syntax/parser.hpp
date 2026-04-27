#pragma once

#include "ast.hpp"
#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "parser_precedence.hpp"
#include "token.hpp"
#include <span>
#include <string_view>

// 我们将解析器分为两部分实现，第一部分为表达式解析器，该部分我们使用普拉特解析算法，
//  第二部分为语句解析器&顶层声明，该部分我们手写递归下降来保证性能。
// 在此之前，我们需要定义一个parser类，来链接表达式解析器和语句/顶层声明解析器。
namespace niki::syntax {

struct ParseResult {
    ASTNodeIndex root = ASTNodeIndex::invalid();
    niki::diagnostic::DiagnosticBag diagnostics;
};

class Parser {
  public:
    // 严禁 Parser 自行分配内存，必须由数据池（astpool） 注入内存池
    Parser(std::string_view source, std::span<const Token> tokens, ASTPool &pool, std::string_view source_path = "");
    // 解析总入口：
    // 1) 驱动 token 游标向前消费
    // 2) 组织顶层声明为 ProgramRoot
    // 3) 在 ASTPool 中落盘节点并返回根索引
    ParseResult parse();

  private:
    std::string_view source;       // 获取字节源
    std::string_view sourcePath;   // 获取字节源路径
    std::span<const Token> tokens; // token窗口
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
    niki::diagnostic::DiagnosticBag diagnostics; // 错误信息返回包

    //---Token 游标控制---
    // 这组函数构成 Parser 的“输入门面”，所有语法分支都依赖它们推进 token 流。
    // 约束：只有这组方法可以修改 tokenIndex/current/previous，避免状态失真。
    void advance();
    void consume(TokenType type, const char *message);
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool isAtEnd(TokenType type);

    //---AST 节点生成器---
    // 所有 parser 子流程最终都通过 emitNode 写入 ASTPool，
    // 以保证位置信息、payload 与节点类型的写入路径统一。
    // 这里我们传入一个空token，用来进行长函数定位
    ASTNodeIndex emitNode(NodeType type, const ASTNodePayload &payload,
                          Token startToken = Token(0, 0, 0, 0, TokenType::TOKEN_EOF));

    //---错误处理---
    // errorAtCurrent: 记录当前 token 的语法错误并拉起 panic 模式。
    // synchronize:    扫描到下一个同步点（如分号/块边界）后恢复解析。
    void errorAtCurrent(const char *message); // 处理parse阶段遇到的错误信息
    void synchronize();

    // 下面的各类解析方法我就不在这里做介绍和解释了，我们到实际的实现里面再讨论。
    // 为了防止单个文件内有过多代码行数，我们将实现以“主实现”，”顶层声明”，“语句”和“表达式”进行分类↓
    // 分别对应parser.cpp|parser_declaration.cpp|parser_statement.cpp|parser_expression.cpp四个实现文件。
    //---Pratt 表达式解析引擎---
    // parseExpression 负责“驱动循环”，parsePrefix/parseInfix 负责“构造节点”。
    // 三者合起来提供运算符优先级解析能力，并支持调用/索引/成员访问等后缀结构。
    ASTNodeIndex parseExpression(Precedence precedence);
    ASTNodeIndex parsePrefix(TokenType type);
    ASTNodeIndex parseInfix(TokenType type, ASTNodeIndex left);
    Precedence getPrecedence(TokenType type) const;

    //---顶层声明解析 (parser_declaration.cpp) ---
    ASTNodeIndex parseTopLevelDeclaration(); // 仅在文件最外层或 module 内使用，严格拒绝表达式语句
    ASTNodeIndex parseDeclaration();         // 允许回退到 parseStatement (用于代码块内部)
    ASTNodeIndex parseFunctionDecl();
    ASTNodeIndex parseStructDecl();
    ASTNodeIndex parseEnumDecl();
    ASTNodeIndex parseTypeAliasDecl();
    ASTNodeIndex parseInterfaceDecl();
    ASTNodeIndex parseImplDecl();
    ASTNodeIndex parseImportDecl();
    ASTNodeIndex parseExportDecl();
    ASTNodeIndex parseModuleDecl();
    ASTNodeIndex parseSystemDecl();
    ASTNodeIndex parseComponentDecl();
    ASTNodeIndex parseFlowDecl();
    ASTNodeIndex parseKitsDecl();
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
};
} // namespace niki::syntax