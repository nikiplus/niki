#include "include/scanner.hpp"

namespace niki::syntax {
/*辅助函数*/
// 判断是否为字母/数字
bool Scanner::isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool Scanner::isDigit(char c) { return c >= '0' && c <= '9'; }

Token Scanner::scanToken() {
    skipWhitespace();
    start = current;
    if (isAtEnd())
        return makeToken(TokenType::TOKEN_EOF);
}
} // namespace niki::syntax