#include "niki/syntax/scanner.hpp"
#include "niki/syntax/token.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <sys/stat.h>

// 在编写一个头文件对应的实现文件时，最好能保证函数的顺序是对应的——最好注释也能对应。
// 不然后面出了bug排查起来有得受的。

namespace niki::syntax {

Scanner::Scanner(std::string_view source) : source(source) {
    start = 0;
    current = 0;
    line = 1;
    lineStart = start;
}

/*写主函数之前，我们先来想想发生了身什么，主函数需要输入什么，又要输出什么？
 *要输出什么很明显，函数的返回值就是Token。
 *而要输入的则是源代码，那我们之前在头文件中是如何获取源代码的呢？
 *通过Scanner的构造函数，将源代码传入Scanner中。
 */
Token Scanner::scanToken() {
    // 扫描主流程：跳过空白 -> 锁定起点 -> 识别 token -> 回传位置信息。
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

    // 识别下划线通配符（必须单独作为一个Token，而不是Identifier的一部分）
    // 注意：如果是 `_foo` 这种，它依然应该被识别为 Identifier。
    // 只有单独的一个 `_` 且后面不跟字母/数字时，才是通配符。
    if (c == '_') {
        if (!isAlpha(peek()) && !isDigit(peek())) {
            return makeToken(TokenType::KW_WILDCARD);
        }
    }

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
        if (match('.')) {
            return makeToken(TokenType::SYM_CONCAT);
        }
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
    return errorToken();
};

/** @section 扫描辅助函数(判断是否到达源字符串末尾，移动游标，查看当前字符等) */
// 想要让主函数能获知当前是个什么个状态，指针指到了哪里，我们就需要一系列辅助函数来进行辅助。
bool Scanner::isAtEnd() { return current >= source.size(); };
// 返回当前字符并移动游标->之前我们在scanner开头有讲，代码是从左往右执行的，因此我们下面这段代码是先”返回“，再”移动“
char Scanner::advance() {
    if (isAtEnd())
        return false;
    return source[current++];
};
/** @note 注意这个”++“!它的意思是current += 1，是有副作用的，会对current指针的位置产生改动的*/
// 查看当前字符但不移动游标
char Scanner::peek() {
    // 进行检查，防止peek到不存在的字符串->这会导致bug！
    if (isAtEnd())
        return false;
    return source[current];
};
// 查看下一个字符但不移动游标
char Scanner::peekNext() {
    // 看看到头了没
    if (isAtEnd())
        return false;
    /** @note 而这个+1，它只是告诉source去看current+1的那个位置，而current本身是不进行修改的[也就是无副作用]*/
    // 再看看指针的下一个位置是不是结束符
    if (current + 1 >= source.size())
        return false;
    // 返回下一个位置的值
    return source[current + 1];
    /** @note 这里的 current[1] 只是向前看一个位置，current 指针本身不会被修改（无副作用）*/
}; // [副作用]这个概念很重要，建议去深入了解一下。
// 匹配当前字符并移动游标
bool Scanner::match(char expected) {
    if (isAtEnd())
        return false;
    if (source[current] != expected)
        return false;
    current++;
    return true;
}; // 匹配当前字符并移动游标

/** @section 判断辅助函数(数字，字母，空格等) */
bool Scanner::isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_'); }
bool Scanner::isDigit(char c) { return c >= '0' && c <= '9'; }
void Scanner::skipWhitespace() {
    // 空白与注释清洗器：持续吞掉无关字符，直到遇到有效 token 起点。
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
    if (this->current - this->start == startOffset + length &&
        source.compare(this->start + startOffset, length, rest) == 0) {
        return type;
    }
    return TokenType::IDENTIFIER;
}

// 检查是否为标识符
TokenType Scanner::checkIdentifierType() {
    int length = static_cast<int>(current - start);
    char c0 = source[start]; // 获取首字母

    // 极致的硬编码 Trie 树，O(1) 无哈希无循环查找
    switch (c0) {
    case 'a':
        if (length == 3)
            return checkKeyword(1, 2, "ny", TokenType::KW_ANY);
        if (length == 5) {
            if (source[start + 1] == 'w')
                return checkKeyword(2, 3, "ait", TokenType::KW_AWAIT);
            if (source[start + 1] == 's')
                return checkKeyword(2, 3, "ync", TokenType::KW_ASYNC);
        }
        break;
    case 'b':
        if (length > 1) {
            switch (source[start + 1]) {
            case 'o':
                return checkKeyword(2, 2, "ol", TokenType::KW_BOOL);
            case 'r':
                return checkKeyword(2, 3, "eak", TokenType::KW_BREAK);
            }
        }
        break;
    case 'c':
        if (length > 1) {
            switch (source[start + 1]) {
            case 'a':
                if (length == 4)
                    return checkKeyword(2, 2, "se", TokenType::KW_CASE);
                break;
            case 'o':
                if (length > 2) {
                    switch (source[start + 2]) {
                    case 'n': // const, continue
                        if (length > 3 && source[start + 3] == 's')
                            return checkKeyword(4, 1, "t", TokenType::KW_CONST);
                        if (length > 3 && source[start + 3] == 't') {
                            if (length == 8)
                                return checkKeyword(4, 4, "inue", TokenType::KW_CONTINUE);
                        }
                        break;
                    case 'm':
                        return checkKeyword(3, 6, "ponent", TokenType::KW_COMPONENT);
                    }
                }
                break;
            }
        }
        break;
    case 'e':
        if (length == 4) {
            if (source[start + 1] == 'l')
                return checkKeyword(2, 2, "se", TokenType::KW_ELSE);
            if (source[start + 1] == 'n')
                return checkKeyword(2, 2, "um", TokenType::KW_ENUM);
        }
        if (length == 9)
            return checkKeyword(1, 8, "xclusive", TokenType::KW_EXCLUSIVE);
        break;
    case 'f':
        if (length > 1) {
            switch (source[start + 1]) {
            case 'a':
                return checkKeyword(2, 3, "lse", TokenType::KW_FALSE);
            case 'l':
                if (length > 2 && source[start + 2] == 'o') {
                    switch (source[start + 3]) {
                    case 'a':
                        return checkKeyword(4, 1, "t", TokenType::KW_FLOAT);
                    case 'w':
                        return checkKeyword(4, 0, "", TokenType::KW_FLOW);
                    }
                }
                break;
            case 'o':
                return checkKeyword(2, 1, "r", TokenType::KW_FOR);
            case 'u':
                return checkKeyword(2, 2, "nc", TokenType::KW_FUNC);
            }
        }
        break;
    case 'i':
        if (length > 1) {
            switch (source[start + 1]) {
            case 'f':
                return checkKeyword(2, 0, "", TokenType::KW_IF);
            case 'm':
                if (length == 4)
                    return checkKeyword(2, 2, "pl", TokenType::KW_IMPL);
                break;
            case 'n':
                if (length == 3)
                    return checkKeyword(2, 1, "t", TokenType::KW_INT);
                if (length == 9)
                    return checkKeyword(2, 7, "terface", TokenType::KW_INTERFACE);
                break;
            }
        }
        break;
    case 'k':
        return checkKeyword(1, 3, "its", TokenType::KW_KITS);
    case 'l':
        return checkKeyword(1, 3, "oop", TokenType::KW_LOOP);
    case 'm':
        if (length == 5)
            return checkKeyword(1, 4, "atch", TokenType::KW_MATCH);
        if (length == 6)
            return checkKeyword(1, 5, "odule", TokenType::KW_MODULE);
        break;
    case 'n':
        if (length == 3)
            return checkKeyword(1, 2, "il", TokenType::KW_NIL);
        if (length == 4)
            return checkKeyword(1, 3, "ock", TokenType::KW_NOCK);
        break;
    case 'r':
        if (length == 4)
            return checkKeyword(1, 3, "ead", TokenType::KW_READ);
        if (length == 6)
            return checkKeyword(1, 5, "eturn", TokenType::KW_RETURN);
        break;
    case 's':
        if (length > 1) {
            switch (source[start + 1]) {
            case 'e':
                return checkKeyword(2, 1, "t", TokenType::KW_SET);
            case 't':
                if (length == 6) {
                    if (source[start + 2] == 'r') {
                        if (source[start + 3] == 'i')
                            return checkKeyword(4, 2, "ng", TokenType::KW_STRING);
                        if (source[start + 3] == 'u')
                            return checkKeyword(4, 2, "ct", TokenType::KW_STRUCT);
                    }
                }
                break;
            case 'y':
                return checkKeyword(2, 4, "stem", TokenType::KW_SYSTEM);
            }
        }
        break;
    case 't':
        if (length > 1) {
            switch (source[start + 1]) {
            case 'a':
                if (length == 3)
                    return checkKeyword(2, 1, "g", TokenType::KW_TAG);
                if (length == 6)
                    return checkKeyword(2, 4, "rget", TokenType::KW_TARGET);
                if (length == 8)
                    return checkKeyword(2, 6, "ggroup", TokenType::KW_TAGGROUP);
                break;
            case 'r':
                return checkKeyword(2, 2, "ue", TokenType::KW_TRUE);
            case 'y':
                return checkKeyword(2, 2, "pe", TokenType::KW_TYPE);
            }
        }
        break;
    case 'u':
        return checkKeyword(1, 4, "nset", TokenType::KW_UNSET);
    case 'v':
        if (length > 1) {
            switch (source[start + 1]) {
            case 'a':
                return checkKeyword(2, 1, "r", TokenType::KW_VAR);
            case 'o':
                return checkKeyword(2, 2, "id", TokenType::KW_VOID);
            }
        }
        break;
    case 'w':
        if (length == 4)
            return checkKeyword(1, 3, "ith", TokenType::KW_WITH);
        if (length == 5)
            return checkKeyword(1, 4, "rite", TokenType::KW_WRITE);
        break;
    }

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
        return errorToken();
    }

    advance();
    if (peek() != '\'') {
        return errorToken();
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
        return errorToken();
    advance();
    return makeToken(TokenType::LITERAL_STRING);
};

/*具体的TOKEN构造函数*/
Token Scanner::makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start_offset = start;
    token.length = static_cast<uint16_t>(current - start);
    token.line = line;
    token.column = static_cast<uint16_t>(start - lineStart + 1); // 统一从列号 1 开始
    return token;

}; // 构造token

// 构造错误token->会返回一段错误信息哦
Token Scanner::errorToken() {
    Token token;
    token.type = TokenType::TOKEN_ERROR;
    token.start_offset = start;
    token.length = static_cast<uint16_t>(current - start);
    token.line = line;
    token.column = static_cast<uint16_t>(start - lineStart + 1);
    return token;
};
} // namespace niki::syntax
