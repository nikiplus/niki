#include "niki/syntax/parser.hpp"
#include "niki/syntax/token.hpp"
#include <cmath>
#include <iostream>
#include <span>

using namespace niki::syntax;
Parser::Parser(std::string_view source, std::span<const Token> tokens, ASTPool &pool)
    : source(source), tokens(tokens), astPool(pool), tokenIndex(0) {
    advance();
}

//---游标控制---
// 注意我们这个不断变化的haderror，其控制着恐慌模式的开启和关闭。
// 在恐慌模式下，游标会将当前正在扫描的token视为错误，并不断将其跳过，直至跳过整个句段并最终返回一个正确的token——否则我们的代码要么不断报错，要么错误的将语句/表达式级联到一起表达。
void Parser::advance() {
    previous = current; // 记住上一个token，给parsePrefix和parseInfix用。

    // 下方皆为防御代码——因为实际的生产环境中我们并不能保证人不犯错。
    // 若写代码的人真的不犯错，那么我们防御代码中的大量内容事实上都可以去掉，只留下这么一行
    // current = tokens[tokenIndex++];
    for (;;) {
        // 仅当token索引位不超出tokens栈长度时才执行，否则会索引到不存在的域
        if (tokenIndex < tokens.size()) {
            current = tokens[tokenIndex++];
        } else {
            // 如果越界，强制制造一个eof
            current = Token{0, 0, 0, 0, TokenType::TOKEN_EOF};
        }
        // 若token为错误（无法识别或错误字符），指针持续向前推进，直至能够返回一个正确的token
        if (current.type != TokenType::TOKEN_ERROR) {
            break;
        }
        // 报告词法错误
        errorAtCurrent("Scanner encountered an invalid character.");
    }
};

void Parser::consume(TokenType type, const char *message) {
    if (current.type == type) {
        advance();
        return;
    }
    errorAtCurrent(message);
};
// 获取当前token类型
bool Parser::check(TokenType type) const { return current.type == type; };
// 获取当前token类型，若匹配，则向前推进指针。
bool Parser::match(TokenType type) {
    if (!check(type))
        return false;
    advance();
    return true;
};

//---AST节点生成器---
ASTNodeIndex Parser::emitNode(NodeType type, const ASTNodePayload &payload) {};

//---错误处理---
void Parser::errorAtCurrent(const char *message) {
    if (panicMode)
        return;
    panicMode = true;
    std::cerr << "[line " << current.line << "] Error";
    if (current.type == TokenType::TOKEN_EOF) {
        std::cerr << "at'" << source.substr(current.start_offset, current.length) << "'";
    }
    std::cerr << ":" << message << "\n";
    hadError = true;
};
void Parser::synchronize() {
    // 退出恐慌模式
    panicMode = false;

    // 不断快进，直到找到下一个能作为开头的同步点。
    while (current.type != TokenType::TOKEN_EOF) {
        if (previous.type == TokenType::SYM_SEMICOLON)
            return;
        switch (current.type) {
        case TokenType::KEYWORD_VAR:
        case TokenType::KEYWORD_FUNC:
        case TokenType::KEYWORD_IF:
        case TokenType::KEYWORD_RETURN:
        case TokenType::KEYWORD_LOOP:
        case TokenType::KEYWORD_MATCH:
        case TokenType::NK_SYSTEM:
        case TokenType::NK_FLOW:
        case TokenType::NK_COMPONENT:
            return;
        default:;
        }
        advance();
    }
};
