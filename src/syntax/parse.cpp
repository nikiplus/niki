#include "niki/syntax/ast.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/parser_precedence.hpp"
#include "niki/syntax/token.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace niki::syntax;

Parser::Parser(std::string_view source, std::span<const Token> tokens, ASTPool &pool)
    : source(source), tokens(tokens), astPool(pool), tokenIndex(0) {
    // 在这里，我们进行了一次估算，平均每2~3个token产生一个ast节点。
    // 借由此，我们可一次性向操作系统申请一块足够长的连续内存块，但同时保持有效长度为0
    // 这可以彻底消灭我们后续解析过程中push_back()可能引发的数据搬运。
    size_t estimated_nodes = tokens.size() / 2;
    // 为文件提前预申请一块内存，防止后续反复申请。
    if (estimated_nodes < 64) {
        estimated_nodes = 64;
    }
    // reserve方法并不改变size。
    astPool.nodes.reserve(estimated_nodes);
    astPool.locations.reserve(estimated_nodes);
    // 边长列表的消耗通常比节点少，我们预估其为1/4
    astPool.lists_elements.reserve(tokens.size() / 4);
    advance();
};

ASTNodeIndex Parser::parse() {
    // 创建一个临时数组以收集所有顶层声明
    std::vector<ASTNodeIndex> declarations;
    while (current.type != TokenType::TOKEN_EOF) {
        size_t startTokenIndex = tokenIndex;

        // 顶层入口：解析声明（Declaration）
        // 如果不是 var/func 等声明关键字，它会自动降级为语句（Statement）甚至表达式（Expression）
        ASTNodeIndex decl = parseDeclaration();

        // 判断是否处于恐慌模式
        if (panicMode) {
            synchronize();
        } else {
            declarations.push_back(decl);
        }
        // 打破死循环，强制跳过当前token
        if (tokenIndex == startTokenIndex) {
            errorAtCurrent("Parser bug: infinite loop detected, forcibly skipping current token.");
            advance();
        }
    }

    // 初始化union
    ASTNodePayload payload{};
    // 创建ModuleDeclPayload
    payload.module_decl.exports = astPool.allocateList(declarations);
    return emitNode(NodeType::ModuleDecl, payload);
};

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
        errorAtCurrent("Invalid lexical token.");
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

bool Parser::isAtEnd(TokenType type) { return current.type == type; };

//---AST节点生成器---
ASTNodeIndex Parser::emitNode(NodeType type, const ASTNodePayload &payload, Token startToken) {
    /*我们首先通过一套类型体操获取将传入的astnode 的位置。
    在这一步进行时，我们的astpool上还未传入我们要传的astnode，简单图例如下。
             node coming!↓
    +----+----+----+----+↓----*---
    |nod0|nod1|nod2|nod3|HERE!|...
    +----+----+----+----+-----+---
    |←std::vector::size→|
    看到吗？我们新传入的node位置的索引，正是之前所有node位置之尾！
    */
    ASTNodeIndex nodeIndex = static_cast<ASTNodeIndex>(astPool.nodes.size());
    astPool.nodes.push_back(ASTNode{type, payload});
    TokenLocation location;

    // 记得我们传入了一个startToken吗？这是为了防止报错偏移
    // 假设我们进入astpool的是一个函数——一个错误函数，其可能包含了数十行数百行甚至数千行代码。已知在恐慌模式下，我们会不断向前推，直到遇到一个新的开始时返回。
    // 那么在这种情况下，我们返回的错误信息的位置事实是这段函数的函数尾——这样的体验实在是太糟糕了！我们无法对着一个括号判断是哪个函数出了问题。
    // 因此在处理类似的长函数时，我们需要记录一下长函数第一个token的位置。
    if (startToken.line == 0) {
        location.line = previous.line;
        location.column = previous.column;
    } else {
        location.line = startToken.line;
        location.column = startToken.column;
    }
    astPool.locations.push_back(location);

    return nodeIndex;
};

//---错误处理---
void Parser::errorAtCurrent(const char *message) {
    if (panicMode)
        return;
    panicMode = true;
    std::cerr << "[line " << current.line << "] Error ";
    if (current.type == TokenType::TOKEN_EOF) {
        std::cerr << "at end";
    } else {
        std::string_view lexeme = source.substr(current.start_offset, current.length);

        if (lexeme.length() > 20) {
            std::cerr << "at'" << lexeme.substr(0, 17) << "...'";

        } else {
            std::cerr << "at'" << lexeme << "'";
        }
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
}
