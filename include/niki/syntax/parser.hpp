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
    ASTNodeIndex emitNode(NodeType type, const ASTNodePayload &payload);

    //---错误处理---
    void errorAtCurrent(const char *message);
    void synchronize();

    //---Pratt 表达式解析引擎---
    ASTNodeIndex parseExpression(Precedence precedence);
    ASTNodeIndex parsePrefix(TokenType type);
    ASTNodeIndex parseInfix(TokenType type, ASTNodeIndex left);
    Precedence getPrecedence(TokenType type) const;
};
} // namespace niki::syntax