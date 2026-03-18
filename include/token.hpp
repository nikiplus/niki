#pragma once
#include <cstdint>

namespace niki::syntax {
enum class TokenType : uint8_t {
    /*---[symbol]符号---*/
    // 单字符符号
    //  括号
    SYM_PAREN_L,   // ( 左括号
    SYM_PAREN_R,   // ) 右括号
    SYM_BRACKET_L, // [ 左中括号
    SYM_BRACKET_R, // ] 右中括号
    SYM_BRACE_L,   // { 左大括号
    SYM_BRACE_R,   // } 右大括号
    // 运算符
    SYM_PLUS,    // + 加法
    SYM_MINUS,   // - 减法
    SYM_STAR,    // * 乘法
    SYM_SLASH,   // / 除法
    SYM_MOD,     // % 取余运算
    SYM_BANG,    // ! 非
    SYM_EQUAL,   // = 赋值
    SYM_GREATER, // > 大于
    SYM_LESS,    // < 小于
    // 位运算符
    SYM_BIT_AND, // & 与运算
    SYM_BIT_OR,  // | 或运算
    SYM_BIT_XOR, // ^ 异或运算
    SYM_BIT_NOT, // ~ 位取反
    // 其它
    SYM_COMMA,     // , 逗号
    SYM_DOT,       // . 点号
    SYM_COLON,     // : 冒号
    SYM_SEMICOLON, // ; 分号
    // 双字符符号
    SYM_AND,           // &&
    SYM_OR,            // ||
    SYM_BANG_EQUAL,    // !=
    SYM_EQUAL_EQUAL,   // ==
    SYM_GREATER_EQUAL, // >=
    SYM_LESS_EQUAL,    // <=
    // 位运算符
    SYM_BIT_SHL, // << 左移
    SYM_BIT_SHR, // >> 右移
    // 快速运算符
    SYM_PLUS_EQUAL,  // += 加法赋值
    SYM_MINUS_EQUAL, // -= 减法赋值
    SYM_STAR_EQUAL,  // *= 乘法赋值
    SYM_SLASH_EQUAL, // /= 除法赋值
    SYM_MOD_EQUAL,   // %= 取余赋值

    /*---[literal]字面量---*/
    LITERAL_INT,    // 整数
    LITERAL_FLOAT,  // 浮点数
    LITERAL_STRING, // 字符串
    LITERAL_CHAR,   // 字符

    /*---[identifier]标识符---*/
    IDENTIFIER, // 标识符

    /*---[keyword]关键字---*/
    // 控制流
    KEYWORD_TRUE,     // true 真
    KEYWORD_FALSE,    // false 假
    KEYWORD_IF,       // if 如果
    KEYWORD_ELSE,     // else 否则
    KEYWORD_WHILE,    // while 循环
    KEYWORD_FOR,      // for 循环
    KEYWORD_BREAK,    // break 跳出循环
    KEYWORD_CONTINUE, // continue 继续循环
    KEYWORD_RETURN,   // return 返回值
    // 变量与函数
    KEYWORD_VAR,   // var
    KEYWORD_FUNC,  // func
    KEYWORD_CONST, // const

    // 类型系统 (静态类型检查基础设施)
    KEYWORD_INT,       // int
    KEYWORD_FLOAT,     // float
    KEYWORD_STRING,    // string
    KEYWORD_BOOL,      // bool
    KEYWORD_VOID,      // void
    KEYWORD_ANY,       // any (逃生舱)
    KEYWORD_TYPE,      // type (类型别名)
    KEYWORD_STRUCT,    // struct (结构体)
    KEYWORD_INTERFACE, // interface (接口)

    /*---[control]控制标记---*/
    TOKEN_EOF,   // 结束标记
    TOKEN_ERROR, // 错误标记

    // 标识符与数据
    NIL, // nil

    // 其他
    COMMENT, // 注释
};
} // namespace niki::syntax
