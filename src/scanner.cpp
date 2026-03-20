#include "include/scanner.hpp"
#include <cstddef>
#include <cstring>
#include <string>
#include <sys/stat.h>

// 在编写一个头文件对应的实现文件时，最好能保证函数的顺序是对应的——最好注释也能对应。
// 不然后面出了bug排查起来有得受的。

namespace niki::syntax {

Scanner::Scanner(std::string_view source) : source(source) {
    start = source.data();
    current = source.data();
    line = 1;
    lineStart = start;
}

/*写主函数之前，我们先来想想发生了身什么，主函数需要输入什么，又要输出什么？
 *要输出什么很明显，函数的返回值就是Token。
 *而要输入的则是源代码，那我们之前在头文件中是如何获取源代码的呢？
 *通过Scanner的构造函数，将源代码传入Scanner中。
 */
Token Scanner::scanToken() {
    // 1.跳过无意义字符
    skipWhitespace();
    // 2.记录当前token的起始位置。
    start = current;
    // 3.是否到达尾
    if (isAtEnd()) {
        return makeToken(TokenType::TOKEN_EOF);
    }
    // 4.读取当前字符并前进指针。
    char c = advance();
    // 5.进行数字和字面量判断
    if (isAlpha(c))
        return makeIdentifierToken();
    if (isDigit(c))
        return makeNumberToken();
    // 6.具体TokenType筛选
    switch (c) {
    case '(':
        return makeToken(TokenType::SYM_PAREN_L);
    case ')':
        return makeToken(TokenType::SYM_PAREN_R);
    case '[':
        return makeToken(TokenType::SYM_BRACKET_L);
    case ']':
        return makeToken(TokenType::SYM_BRACKET_R);
    case '{':
        return makeToken(TokenType::SYM_BRACE_L);
    case '}':
        return makeToken(TokenType::SYM_BRACE_R);
    case '+':
        // 这里使用了三元判断，三元判断格式如下
        //(条件)？为真:为假
        // 例子:(a>3)?2:1
        // 在这个例子中，当a大于3时，返回2，否则返回1
        return makeToken(match('=') ? TokenType::SYM_PLUS_EQUAL : TokenType::SYM_PLUS);
    case '-':
        // 超过两个返回结果，因此我们使用if else if来进行多重判断
        if (match('>')) {
            return makeToken(TokenType::SYM_ARROW);
        } else if (match('=')) {
            return makeToken(TokenType::SYM_MINUS_EQUAL);
        }
        return makeToken(TokenType::SYM_MINUS);
    case '*':
        return makeToken(match('=') ? TokenType::SYM_STAR_EQUAL : TokenType::SYM_STAR);
    case '/':
        if (match('=')) {
            return makeToken(TokenType::SYM_SLASH_EQUAL);
        }
        return makeToken(TokenType::SYM_SLASH);
    case '%':
        return makeToken(match('=') ? TokenType::SYM_MOD_EQUAL : TokenType::SYM_MOD);
    case '!':
        return makeToken(match('=') ? TokenType::SYM_BANG_EQUAL : TokenType::SYM_BANG);
    case '=':
        if (match('=')) {
            return makeToken(TokenType::SYM_EQUAL_EQUAL);
        } else if (match('>')) {
            return makeToken(TokenType::SYM_FAT_ARROW);
        };
        return makeToken(TokenType::SYM_EQUAL);
    case '>':
        if (match('=')) {
            return makeToken(TokenType::SYM_GREATER_EQUAL);
        } else if (match('>')) {
            return makeToken(TokenType::SYM_BIT_SHR);
        }
        return makeToken(TokenType::SYM_GREATER);
    case '<':
        if (match('=')) {
            return makeToken(TokenType::SYM_LESS_EQUAL);
        } else if (match('<')) {
            return makeToken(TokenType::SYM_BIT_SHL);
        }
        return makeToken(TokenType::SYM_LESS);
    case '&':
        if (match('&')) {
            return makeToken(TokenType::SYM_AND);
        } else if (match('=')) {
            return makeToken(TokenType::SYM_BIT_AND_EQUAL);
        }
        return makeToken(TokenType::SYM_BIT_AND);
    case '|':
        if (match('|')) {
            return makeToken(TokenType::SYM_OR);
        } else if (match('=')) {
            return makeToken(TokenType::SYM_BIT_OR_EQUAL);
        }
        return makeToken(TokenType::SYM_BIT_OR);
    case '^':
        return makeToken(match('=') ? TokenType::SYM_BIT_XOR_EQUAL : TokenType::SYM_BIT_XOR);
    case '~':
        // 未来这里可能会有~的新用处，不过现在没有
        return makeToken(TokenType::SYM_BIT_NOT);
    case ',':
        return makeToken(TokenType::SYM_COMMA);
    case '.':
        return makeToken(TokenType::SYM_DOT);
    case ':':
        return makeToken(match(':') ? TokenType::SYM_SCOPE : TokenType::SYM_COLON);
    case ';':
        return makeToken(TokenType::SYM_SEMICOLON);
    case '?':
        return makeToken(TokenType::SYM_QUESTION);
    case '@':
        return makeToken(TokenType::SYM_AT);
    case '"':
        return makeStringToken();
    case '\'':
        return makeCharToken();
    }
    return errorToken("Scanner:未知字符串!");
};

/** @section 扫描辅助函数(判断是否到达源字符串末尾，移动游标，查看当前字符等) */
// 想要让主函数能获知当前是个什么个状态，指针指到了哪里，我们就需要一系列辅助函数来进行辅助。
bool Scanner::isAtEnd() { return *current == '\0'; };
// 返回当前字符并移动游标->之前我们在scanner开头有讲，代码是从左往右执行的，因此我们下面这段代码是先”返回“，再”移动“
char Scanner::advance() { return *current++; };
/** @note 注意这个”++“!它的意思是current += 1，是有副作用的，会对current指针的位置产生改动的*/
// 查看当前字符但不移动游标
char Scanner::peek() {
    // 进行检查，防止peek到不存在的字符串->这会导致bug！
    if (isAtEnd())
        return '\0';
    return *current;
};
// 查看下一个字符但不移动游标
char Scanner::peekNext() {
    // 看看到头了没
    if (isAtEnd())
        return '\0';
    /** @note 而这个+1，它只是告诉source去看current+1的那个位置，而current本身是不进行修改的[也就是无副作用]*/
    // 再看看指针的下一个位置是不是结束符
    if (*(current + 1) == '\0')
        return '\0';
    // 返回下一个位置的值
    return current[1];
    /** @note 这里的 current[1] 只是向前看一个位置，current 指针本身不会被修改（无副作用）*/
}; // [副作用]这个概念很重要，建议去深入了解一下。
// 匹配当前字符并移动游标
bool Scanner::match(char expected) {
    if (isAtEnd())
        return '\0';
    if (*current != expected)
        return false;
    current++;
    return true;
}; // 匹配当前字符并移动游标

/** @section 判断辅助函数(数字，字母，空格等) */
bool Scanner::isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool Scanner::isDigit(char c) { return c >= '0' && c <= '9'; }
void Scanner::skipWhitespace() {
    while (true) {
        char c = peek();
        switch (c) {
        case ' ':
        case '\t':
        case '\r':
            advance();
            break;

        case '\n':
            line++;
            advance();
            lineStart = current;
            break;

        case '/':
            if (peekNext() == '/') { // 单行注释，扫描器扫描到换行符就晓得要开始下一段了
                // 注意：这里不要处理 \n。因为外层的 while(true) 会在下一轮循环中
                // 遇到 \n，并交给外层的 case '\n': 去统一处理 line 和 lineStart！
                while (peek() != '\n' && !isAtEnd()) {
                    advance();
                }
            } else if (peekNext() == '*') {
                // 多行注释
                advance(); // 吃掉‘/’
                advance(); // 吃掉‘*’
                while (!isAtEnd()) {
                    if (peek() == '\n') {
                        line++;
                        advance();           // 吃掉换行符
                        lineStart = current; // 必须更新行首指针！
                    } else if (peek() == '*' && peekNext() == '/') {
                        advance(); // 吃掉‘*’
                        advance(); // 吃掉‘/’
                        break;
                    } else {
                        advance(); // 吃掉其他字符
                    }
                }
            } else {
                return;
            }
            break;

        default:
            return;
        }
    }
} // namespace niki::syntax

/** @section 判断标识符 */
// 检查是否为关键词。如果是，返回对应的 TokenType；否则返回普通的 IDENTIFIER。
TokenType Scanner::checkKeyword(int startOffset, int length, const char *rest, TokenType type) {
    // 在这里，我们先创建一个“sub指针”，用来指向即将访问的字符串的地址
    const char *current_word = start + startOffset;
    // 2. 利用 && 的短路求值特性进行两阶段比对：
    // 第一阶段（廉价）：检查“当前扫到的长度”是否与“目标关键词长度”完全一致。
    // 第二阶段（昂贵但精确）：调用 memcmp 直接对比内存字节流，确认内容是否一致。
    // 经过这两次判断后，若其大小和我们tokentype里定义的type是对等的，则我们可以视为其为某个指定类型的token
    if (this->current - this->start == startOffset + length && memcmp(current_word, rest, length) == 0) {
        return type;
    }
    return TokenType::IDENTIFIER;
};

// 检查是否为标识符->@TokenType: 返回的是（TOKEN_IDENTIFIER）
TokenType Scanner::checkIdentifierType() {
    // 看，我们上面刚刚构建好的checkKeyword就在这里用到了。
    // 这儿用了树状思路来判断那些前几个字符类似的标识符，比如"false""flow"。
    // 我们对各类关键字以长度进行分类。
    int length = current - start;
    /**
     * @brief [CHECK_KW]来讲解为什么设计了下面这个宏
     * * @details 该宏采用两阶段匹配策略以提升性能：
     * 1. 预先检查首字母是否匹配。(块比较)
     * 2. 使用 memcmp 批量比较剩余字符。(慢比较)
     * tips:别忘了代码是从左往右执行的，在各类比较中，若左侧比较成立，编译器会直接跳转结果，而不会继续检查右侧条件是否成立
     * * @note **关于长度计算 `sizeof(str) - 2` 的原理解析：**
     * C++ 字符串字面量（如 "abc"）在内存中会自动包含一个不可见的结束符 `\0`。
     * @code
     * str = "abc"
     * +---+---+---+---+
     * | a | b | c | \0|
     * +---+---+---+---+
     * ^           ^
     * start[0]    sizeof(str) = 4
     * @endcode
     * * 计算剩余需要比较的长度逻辑如下：
     * - **sizeof(str)**: 获取总字节数（包含 `\0`）。
     * - **-1**: 减去末尾自动生成的结束符 `\0`。
     * - **-1**: 减去已经在外部 Switch/If 中判断过的首字母。
     * - **结果**: `sizeof(str) - 2` 即为剩余有效负载（Payload）的长度。
     * * @warning
     * - 宏具有特定的作用域，在使用完毕后需及时通过 `#undef` 释放，以防污染后续代码空间。
     * - 该宏仅适用于字符串字面量，不可用于 `const char*` 变量。
     */
#define CHECK_KW(str, type)                                                                                            \
    if (start[0] == str[0] && (sizeof(str) == 2 || memcmp(start + 1, &str[1], sizeof(str) - 2) == 0))                  \
        return type;
    // 先筛选长度，再通过宏防止与其它关键字首字母相同，以及判断是否为关键字，
    switch (length) {
    case 2:
        CHECK_KW("if", TokenType::KEYWORD_IF);
        break;
    case 3:
        CHECK_KW("var", TokenType::KEYWORD_VAR);
        CHECK_KW("any", TokenType::KEYWORD_ANY);
        CHECK_KW("tag", TokenType::NK_TAG);
        CHECK_KW("nil", TokenType::NIL);
        CHECK_KW("int", TokenType::KEYWORD_INT);
        CHECK_KW("set", TokenType::NK_SET);
        break;
    case 4:
        CHECK_KW("true", TokenType::KEYWORD_TRUE);
        CHECK_KW("else", TokenType::KEYWORD_ELSE);
        CHECK_KW("loop", TokenType::KEYWORD_LOOP);
        CHECK_KW("func", TokenType::KEYWORD_FUNC);
        CHECK_KW("bool", TokenType::KEYWORD_BOOL);
        CHECK_KW("void", TokenType::KEYWORD_VOID);
        CHECK_KW("flow", TokenType::NK_FLOW);
        CHECK_KW("nock", TokenType::NK_FLOW_NOCK);
        CHECK_KW("type", TokenType::KEYWORD_TYPE);
        CHECK_KW("with", TokenType::NK_WITH);
        CHECK_KW("read", TokenType::NK_READ);
        CHECK_KW("enum", TokenType::NK_ENUM);
        break;
    case 5:
        CHECK_KW("false", TokenType::KEYWORD_FALSE);
        CHECK_KW("break", TokenType::KEYWORD_BREAK);
        CHECK_KW("const", TokenType::KEYWORD_CONST);
        CHECK_KW("match", TokenType::KEYWORD_MATCH);
        CHECK_KW("await", TokenType::NK_FLOW_AWAIT);
        CHECK_KW("async", TokenType::NK_FLOW_ASYNC);
        CHECK_KW("float", TokenType::KEYWORD_FLOAT);
        CHECK_KW("unset", TokenType::NK_UNSET);
        CHECK_KW("write", TokenType::NK_WRITE);
        break;
    case 6:
        CHECK_KW("return", TokenType::KEYWORD_RETURN);
        CHECK_KW("struct", TokenType::NK_STRUCT);
        CHECK_KW("target", TokenType::NK_TARGET);
        CHECK_KW("system", TokenType::NK_SYSTEM);
        CHECK_KW("string", TokenType::KEYWORD_STRING);
        CHECK_KW("module", TokenType::NK_MODULE);
        break;
    case 7:
        CHECK_KW("context", TokenType::NK_CONTEXT);
        break;
    case 8:
        CHECK_KW("continue", TokenType::KEYWORD_CONTINUE);
        CHECK_KW("taggroup", TokenType::NK_TAGGROUP);
        break;
    case 9:
        CHECK_KW("component", TokenType::NK_COMPONENT);
        CHECK_KW("interface", TokenType::KEYWORD_INTERFACE);
        CHECK_KW("exclusive", TokenType::NK_EXCLUSIVE);
        break;
    }

#undef CHECK_KW

    return TokenType::IDENTIFIER;
};

// 构造标识符token
Token Scanner::makeIdentifierToken() {
    while (isAlpha(peek()) || isDigit(peek())) {
        advance();
    }
    return makeToken(checkIdentifierType());
};
// 构造数字token
Token Scanner::makeNumberToken() {
    // 默认是整数
    TokenType type = TokenType::LITERAL_INT;
    // 一直吃掉前面的数字
    while (isDigit(peek())) {
        advance();
    }
    // 检查是否有小数点，并且小数点后面必须跟着数字
    // (防止把方法调用 123.method() 里的点当成小数点)
    if (peek() == '.' && isDigit(peekNext())) {
        // 是浮点数！
        type = TokenType::LITERAL_FLOAT;
        // 吃掉那个 '.'
        advance();
        // 吃掉小数点后面的所有数字
        while (isDigit(peek())) {
            advance();
        }
    }
    return makeToken(type);
};
Token Scanner::makeCharToken() {
    if (isAtEnd()) {
        return errorToken("Scanner:未闭合字符(char.)");
    }

    advance();
    if (peek() != '\'') {
        return errorToken("Scanner:未闭合字符(char)或字符(char)过长");
    }
    advance();
    return makeToken(TokenType::LITERAL_CHAR);
};
Token Scanner::makeStringToken() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') {
            line++;
            advance();
            lineStart = current;
        } else {
            advance();
        }
    }
    if (isAtEnd())
        return errorToken("Scanner:未闭合字符串(string.)");
    advance();
    return makeToken(TokenType::LITERAL_STRING);
};

/*具体的TOKEN构造函数*/
Token Scanner::makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = start;
    token.length = int(current - start);
    token.line = line;
    token.column = int(start - lineStart) + 1; // 统一从列号 1 开始
    return token;

}; // 构造token

// 构造错误token->会返回一段错误信息哦
Token Scanner::errorToken(const char *message) {
    Token token;
    token.type = TokenType::TOKEN_ERROR;
    // 注意，我们在上面传来了一段message对吗？那么我们现在想通过token将这个message传(return)出去，因此就有了下面这段
    token.start = message;               // 看，start指针直接指向了message的地址！
    token.length = (int)strlen(message); // 长度就是message这个字符串的长度
    // 经过上面两步操作，我们就得到了一个，名为TOKEN_ERROR的，包含有指向错误信息的指针的Token
    // 这样是零内存分配的——无需拷贝或new，性能极快
    token.line = line;
    token.column = int(start - lineStart) + 1; // 别忘了列号也要 +1
    return token;
};
} // namespace niki::syntax
