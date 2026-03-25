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

TEST(ScannerTest, AllKeywordsCoverage) {
    std::string source = "true false if else loop match break continue return var func const int float string bool "
                         "void any type interface flow await nock async system component target tag taggroup exclusive "
                         "set unset context with read write module struct enum";
    Scanner scanner(source);

    std::vector<TokenType> expectedTokens = {
        TokenType::KEYWORD_TRUE,   TokenType::KEYWORD_FALSE, TokenType::KEYWORD_IF,     TokenType::KEYWORD_ELSE,
        TokenType::KEYWORD_LOOP,   TokenType::KEYWORD_MATCH, TokenType::KEYWORD_BREAK,  TokenType::KEYWORD_CONTINUE,
        TokenType::KEYWORD_RETURN, TokenType::KEYWORD_VAR,   TokenType::KEYWORD_FUNC,   TokenType::KEYWORD_CONST,
        TokenType::KEYWORD_INT,    TokenType::KEYWORD_FLOAT, TokenType::KEYWORD_STRING, TokenType::KEYWORD_BOOL,
        TokenType::KEYWORD_VOID,   TokenType::KEYWORD_ANY,   TokenType::KEYWORD_TYPE,   TokenType::KEYWORD_INTERFACE,
        TokenType::NK_FLOW,        TokenType::NK_FLOW_AWAIT, TokenType::NK_FLOW_NOCK,   TokenType::NK_FLOW_ASYNC,
        TokenType::NK_SYSTEM,      TokenType::NK_COMPONENT,  TokenType::NK_TARGET,      TokenType::NK_TAG,
        TokenType::NK_TAGGROUP,    TokenType::NK_EXCLUSIVE,  TokenType::NK_SET,         TokenType::NK_UNSET,
        TokenType::NK_CONTEXT,     TokenType::NK_WITH,       TokenType::NK_READ,        TokenType::NK_WRITE,
        TokenType::NK_MODULE,      TokenType::NK_STRUCT,     TokenType::NK_ENUM,        TokenType::TOKEN_EOF};

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
        EXPECT_STREQ(std::string(t.start, t.length).c_str(), "Scanner:未知字符串!");
    }
    {
        std::string source = "\"未闭合的字符串";
        Scanner scanner(source);
        Token t = scanner.scanToken();
        EXPECT_EQ(t.type, TokenType::TOKEN_ERROR);
        EXPECT_STREQ(std::string(t.start, t.length).c_str(), "Scanner:未闭合字符串(string.)");
    }
    {
        std::string source = "\'";
        Scanner scanner(source);
        Token t = scanner.scanToken();
        EXPECT_EQ(t.type, TokenType::TOKEN_ERROR);
        EXPECT_STREQ(std::string(t.start, t.length).c_str(), "Scanner:未闭合字符(char.)");
    }
    {
        std::string source = "\'ab\'";
        Scanner scanner(source);
        Token t = scanner.scanToken();
        EXPECT_EQ(t.type, TokenType::TOKEN_ERROR);
        EXPECT_STREQ(std::string(t.start, t.length).c_str(), "Scanner:未闭合字符(char)或字符(char)过长");
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

    checkToken(TokenType::KEYWORD_VAR, 1, 1);
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
