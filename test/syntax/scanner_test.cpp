#include "niki/syntax/scanner.hpp"
#include "niki/syntax/token.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>

using namespace niki::syntax;

TEST(ScannerTest, AllSymbolsCoverage) {
    std::string source =
        "( ) [ ] { } + - * / % ! = > < & | ^ ~ , . : ; ? @ :: && || != == >= <= << >> &= |= ^= += -= *= /= %= -> =>";
    Scanner scanner(source);

    std::vector<TokenType> expectedTokens = {TokenType::SYM_PAREN_L,
                                             TokenType::SYM_PAREN_R,
                                             TokenType::SYM_BRACKET_L,
                                             TokenType::SYM_BRACKET_R,
                                             TokenType::SYM_BRACE_L,
                                             TokenType::SYM_BRACE_R,
                                             TokenType::SYM_PLUS,
                                             TokenType::SYM_MINUS,
                                             TokenType::SYM_STAR,
                                             TokenType::SYM_SLASH,
                                             TokenType::SYM_MOD,
                                             TokenType::SYM_BANG,
                                             TokenType::SYM_EQUAL,
                                             TokenType::SYM_GREATER,
                                             TokenType::SYM_LESS,
                                             TokenType::SYM_BIT_AND,
                                             TokenType::SYM_BIT_OR,
                                             TokenType::SYM_BIT_XOR,
                                             TokenType::SYM_BIT_NOT,
                                             TokenType::SYM_COMMA,
                                             TokenType::SYM_DOT,
                                             TokenType::SYM_COLON,
                                             TokenType::SYM_SEMICOLON,
                                             TokenType::SYM_QUESTION,
                                             TokenType::SYM_AT,
                                             TokenType::SYM_SCOPE,
                                             TokenType::SYM_AND,
                                             TokenType::SYM_OR,
                                             TokenType::SYM_BANG_EQUAL,
                                             TokenType::SYM_EQUAL_EQUAL,
                                             TokenType::SYM_GREATER_EQUAL,
                                             TokenType::SYM_LESS_EQUAL,
                                             TokenType::SYM_BIT_SHL,
                                             TokenType::SYM_BIT_SHR,
                                             TokenType::SYM_BIT_AND_EQUAL,
                                             TokenType::SYM_BIT_OR_EQUAL,
                                             TokenType::SYM_BIT_XOR_EQUAL,
                                             TokenType::SYM_PLUS_EQUAL,
                                             TokenType::SYM_MINUS_EQUAL,
                                             TokenType::SYM_STAR_EQUAL,
                                             TokenType::SYM_SLASH_EQUAL,
                                             TokenType::SYM_MOD_EQUAL,
                                             TokenType::SYM_ARROW,
                                             TokenType::SYM_FAT_ARROW,
                                             TokenType::TOKEN_EOF};

    for (auto expected : expectedTokens) {
        Token t = scanner.scanToken();
        EXPECT_EQ(t.type, expected);
    }
}

TEST(ScannerTest, AllKWsCoverage) {
    std::string source = "true false if else loop match break continue return var func const int float string bool "
                         "void any type interface flow await nock async system component target tag taggroup exclusive "
                         "set unset kits with read write module struct enum";
    Scanner scanner(source);

    std::vector<TokenType> expectedTokens = {
        TokenType::KW_TRUE,     TokenType::KW_FALSE,     TokenType::KW_IF,     TokenType::KW_ELSE,
        TokenType::KW_LOOP,     TokenType::KW_MATCH,     TokenType::KW_BREAK,  TokenType::KW_CONTINUE,
        TokenType::KW_RETURN,   TokenType::KW_VAR,       TokenType::KW_FUNC,   TokenType::KW_CONST,
        TokenType::KW_INT,      TokenType::KW_FLOAT,     TokenType::KW_STRING, TokenType::KW_BOOL,
        TokenType::KW_VOID,     TokenType::KW_ANY,       TokenType::KW_TYPE,   TokenType::KW_INTERFACE,
        TokenType::KW_FLOW,     TokenType::KW_AWAIT,     TokenType::KW_NOCK,   TokenType::KW_ASYNC,
        TokenType::KW_SYSTEM,   TokenType::KW_COMPONENT, TokenType::KW_TARGET, TokenType::KW_TAG,
        TokenType::KW_TAGGROUP, TokenType::KW_EXCLUSIVE, TokenType::KW_SET,    TokenType::KW_UNSET,
        TokenType::KW_KITS,     TokenType::KW_WITH,      TokenType::KW_READ,   TokenType::KW_WRITE,
        TokenType::KW_MODULE,   TokenType::KW_STRUCT,    TokenType::KW_ENUM,   TokenType::TOKEN_EOF};

    for (auto expected : expectedTokens) {
        Token t = scanner.scanToken();
        EXPECT_EQ(t.type, expected);
    }
}

TEST(ScannerTest, ErrorCharacterRecognition) {
    {
        std::string source = "$";
        Scanner scanner(source);
        Token t = scanner.scanToken();
        EXPECT_EQ(t.type, TokenType::TOKEN_ERROR);
        EXPECT_STREQ(std::string(t.start_offset, t.length).c_str(), "Scanner:未知字符串!");
    }
    {
        std::string source = "\"未闭合的字符串";
        Scanner scanner(source);
        Token t = scanner.scanToken();
        EXPECT_EQ(t.type, TokenType::TOKEN_ERROR);
        EXPECT_STREQ(std::string(t.start_offset, t.length).c_str(), "Scanner:未闭合字符串(string.)");
    }
    {
        std::string source = "\'";
        Scanner scanner(source);
        Token t = scanner.scanToken();
        EXPECT_EQ(t.type, TokenType::TOKEN_ERROR);
        EXPECT_STREQ(std::string(t.start_offset, t.length).c_str(), "Scanner:未闭合字符(char.)");
    }
    {
        std::string source = "\'ab\'";
        Scanner scanner(source);
        Token t = scanner.scanToken();
        EXPECT_EQ(t.type, TokenType::TOKEN_ERROR);
        EXPECT_STREQ(std::string(t.start_offset, t.length).c_str(), "Scanner:未闭合字符(char)或字符(char)过长");
    }
}

TEST(ScannerTest, LineAndColumnAccuracy) {
    std::string source = "var a = 1;\n  b = 2;\n\n c = 3;";
    Scanner scanner(source);

    auto checkToken = [&](TokenType expectedType, int expectedLine, int expectedCol) {
        Token t = scanner.scanToken();
        EXPECT_EQ(t.type, expectedType);
        EXPECT_EQ(t.line, expectedLine);
        EXPECT_EQ(t.column, expectedCol);
    };

    checkToken(TokenType::KW_VAR, 1, 1);
    checkToken(TokenType::IDENTIFIER, 1, 5);
    checkToken(TokenType::SYM_EQUAL, 1, 7);
    checkToken(TokenType::LITERAL_INT, 1, 9);
    checkToken(TokenType::SYM_SEMICOLON, 1, 10);

    checkToken(TokenType::IDENTIFIER, 2, 3);
    checkToken(TokenType::SYM_EQUAL, 2, 5);
    checkToken(TokenType::LITERAL_INT, 2, 7);
    checkToken(TokenType::SYM_SEMICOLON, 2, 8);

    checkToken(TokenType::IDENTIFIER, 4, 2);
    checkToken(TokenType::SYM_EQUAL, 4, 4);
    checkToken(TokenType::LITERAL_INT, 4, 6);
    checkToken(TokenType::SYM_SEMICOLON, 4, 7);
    checkToken(TokenType::TOKEN_EOF, 4, 8);
}

TEST(ScannerTest, PerformanceTest) {
    std::string source;
    source.reserve(1000000);
    for (int i = 0; i < 10000; i++) {
        source += "var x = 10 + 20 * 30;\n";
        source += "if (x > 50) { return true; }\n";
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    Scanner scanner(source);
    int tokenCount = 0;
    while (true) {
        Token t = scanner.scanToken();
        tokenCount++;
        if (t.type == TokenType::TOKEN_EOF || t.type == TokenType::TOKEN_ERROR) {
            break;
        }
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    EXPECT_GT(tokenCount, 100000);
    std::cout << "[ PERFORMANCE ] Scanned " << tokenCount << " tokens, time cost " << duration.count() << " ms\n";
}
