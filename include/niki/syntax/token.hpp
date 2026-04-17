#pragma once
#include <cstdint>
#include <string_view>
/*一定要注意，虽然token和Opcode中很多枚举非常类似，但实际上二者在职责和维度上完全不在一个层面。
不要直接在脑海中构建，sym_plus=op_Add的映射关系，前者是符号，后者是实际执行的命令。
我们一定要明确，符号只有被parser翻译为表达式之后，才真切的成为了compiler能解析的opcode
*/
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
    SYM_CONCAT,    // .. 字符串拼接
    SYM_COLON,     // : 冒号
    SYM_SEMICOLON, // ; 分号
    SYM_QUESTION,  // ? 问号
    SYM_AT,        //@
    // 双字符符号
    // 其它
    SYM_SCOPE, // :: 作用域运算符
    // 逻辑运算符
    SYM_AND,           // &&
    SYM_OR,            // ||
    SYM_BANG_EQUAL,    // !=
    SYM_EQUAL_EQUAL,   // ==
    SYM_GREATER_EQUAL, // >=
    SYM_LESS_EQUAL,    // <=
    // 位运算符
    SYM_BIT_SHL,       // << 左移
    SYM_BIT_SHR,       // >> 右移
    SYM_BIT_AND_EQUAL, // &= 与赋值
    SYM_BIT_OR_EQUAL,  // |= 或赋值
    SYM_BIT_XOR_EQUAL, // ^= 异或赋值
    // 快速运算符
    SYM_PLUS_EQUAL,  // += 加法赋值
    SYM_MINUS_EQUAL, // -= 减法赋值
    SYM_STAR_EQUAL,  // *= 乘法赋值
    SYM_SLASH_EQUAL, // /= 除法赋值
    SYM_MOD_EQUAL,   // %= 取余赋值
    // 箭头
    SYM_ARROW,     // -> 箭头
    SYM_FAT_ARROW, // => 胖箭头

    /*---[literal]字面量---*/
    LITERAL_INT,    // 整数
    LITERAL_FLOAT,  // 浮点数
    LITERAL_STRING, // 字符串
    LITERAL_CHAR,   // 字符

    /*---[identifier]标识符---*/
    IDENTIFIER, // 标识符

    /*---[keyword]关键字---*/
    KW_NIL,       // nil
    KW_TRUE,      // true 真
    KW_FALSE,     // false 假
    KW_IF,        // if 如果
    KW_ELSE,      // else 否则
    KW_LOOP,      // loop 循环
    KW_MATCH,     // match 匹配
    KW_CASE,      // case 匹配分支
    KW_BREAK,     // break 跳出循环
    KW_CONTINUE,  // continue 继续循环
    KW_RETURN,    // return 返回值
    KW_FOR,       // for 介词 (用于 impl Interface for Target 等)
    KW_VAR,       // var
    KW_FUNC,      // func
    KW_CONST,     // const
    KW_INT,       // int
    KW_FLOAT,     // float
    KW_STRING,    // string
    KW_BOOL,      // bool
    KW_VOID,      // void
    KW_ANY,       // any (逃生舱)
    KW_TYPE,      // type (类型别名)
    KW_INTERFACE, // interface (接口)
    KW_IMPL,      // impl (实现)
    KW_WILDCARD,  // _ (通配符)
    KW_FLOW,      // flow 声明一个流程
    KW_AWAIT,     // await 挂起并等待某结果
    KW_NOCK,      // nock 主动放弃当前帧执行权
    KW_ASYNC,     // async 异步函数
    KW_SYSTEM,    // system 系统
    KW_COMPONENT, // component 组件
    KW_TARGET,    // target 目标
    KW_TAG,       // tag 标签
    KW_TAGGROUP,  // taggroup 标签组
    KW_EXCLUSIVE, // exclusive 互斥修饰符
    KW_SET,       // set 设置
    KW_UNSET,     // unset 取消设置
    KW_KITS,      // kits
    KW_WITH,      // with 引入模块
    KW_READ,      // read 读取模块
    KW_WRITE,     // write 写入模块
    KW_MODULE,    // module (模块)
    KW_STRUCT,    // struct (结构体)
    KW_ENUM,      // enum (枚举)

    /*---[control]控制标记---*/
    TOKEN_EOF,   // 结束标记
    TOKEN_ERROR, // 错误标记

    /*---[trivia]附属标记---*/
    COMMENT, // 注释
};
/**
 * @brief Token结构
 * type:  token类型
 * start: token在源字符串中的起始位置
 * length: token的长度
 * line: token所在的行号
 */
struct Token {
    uint32_t start_offset;
    uint32_t line;
    uint32_t column;
    uint16_t length;
    TokenType type;
};

inline std::string_view toString(TokenType type) {
    switch (type) {
    case TokenType::SYM_PAREN_L:
        return "SYM_PAREN_L";
    case TokenType::SYM_PAREN_R:
        return "SYM_PAREN_R";
    case TokenType::SYM_BRACKET_L:
        return "SYM_BRACKET_L";
    case TokenType::SYM_BRACKET_R:
        return "SYM_BRACKET_R";
    case TokenType::SYM_BRACE_L:
        return "SYM_BRACE_L";
    case TokenType::SYM_BRACE_R:
        return "SYM_BRACE_R";
    case TokenType::SYM_PLUS:
        return "SYM_PLUS";
    case TokenType::SYM_MINUS:
        return "SYM_MINUS";
    case TokenType::SYM_STAR:
        return "SYM_STAR";
    case TokenType::SYM_SLASH:
        return "SYM_SLASH";
    case TokenType::SYM_MOD:
        return "SYM_MOD";
    case TokenType::SYM_BANG:
        return "SYM_BANG";
    case TokenType::SYM_EQUAL:
        return "SYM_EQUAL";
    case TokenType::SYM_GREATER:
        return "SYM_GREATER";
    case TokenType::SYM_LESS:
        return "SYM_LESS";
    case TokenType::SYM_BIT_AND:
        return "SYM_BIT_AND";
    case TokenType::SYM_BIT_OR:
        return "SYM_BIT_OR";
    case TokenType::SYM_BIT_XOR:
        return "SYM_BIT_XOR";
    case TokenType::SYM_BIT_NOT:
        return "SYM_BIT_NOT";
    case TokenType::SYM_COMMA:
        return "SYM_COMMA";
    case TokenType::SYM_DOT:
        return "SYM_DOT";
    case TokenType::SYM_CONCAT:
        return "SYM_CONCAT";
    case TokenType::SYM_COLON:
        return "SYM_COLON";
    case TokenType::SYM_SEMICOLON:
        return "SYM_SEMICOLON";
    case TokenType::SYM_QUESTION:
        return "SYM_QUESTION";
    case TokenType::SYM_AT:
        return "SYM_AT";
    case TokenType::SYM_SCOPE:
        return "SYM_SCOPE";
    case TokenType::SYM_AND:
        return "SYM_AND";
    case TokenType::SYM_OR:
        return "SYM_OR";
    case TokenType::SYM_BANG_EQUAL:
        return "SYM_BANG_EQUAL";
    case TokenType::SYM_EQUAL_EQUAL:
        return "SYM_EQUAL_EQUAL";
    case TokenType::SYM_GREATER_EQUAL:
        return "SYM_GREATER_EQUAL";
    case TokenType::SYM_LESS_EQUAL:
        return "SYM_LESS_EQUAL";
    case TokenType::SYM_BIT_SHL:
        return "SYM_BIT_SHL";
    case TokenType::SYM_BIT_SHR:
        return "SYM_BIT_SHR";
    case TokenType::SYM_BIT_AND_EQUAL:
        return "SYM_BIT_AND_EQUAL";
    case TokenType::SYM_BIT_OR_EQUAL:
        return "SYM_BIT_OR_EQUAL";
    case TokenType::SYM_BIT_XOR_EQUAL:
        return "SYM_BIT_XOR_EQUAL";
    case TokenType::SYM_PLUS_EQUAL:
        return "SYM_PLUS_EQUAL";
    case TokenType::SYM_MINUS_EQUAL:
        return "SYM_MINUS_EQUAL";
    case TokenType::SYM_STAR_EQUAL:
        return "SYM_STAR_EQUAL";
    case TokenType::SYM_SLASH_EQUAL:
        return "SYM_SLASH_EQUAL";
    case TokenType::SYM_MOD_EQUAL:
        return "SYM_MOD_EQUAL";
    case TokenType::SYM_ARROW:
        return "SYM_ARROW";
    case TokenType::SYM_FAT_ARROW:
        return "SYM_FAT_ARROW";
    case TokenType::LITERAL_INT:
        return "LITERAL_INT";
    case TokenType::LITERAL_FLOAT:
        return "LITERAL_FLOAT";
    case TokenType::LITERAL_STRING:
        return "LITERAL_STRING";
    case TokenType::LITERAL_CHAR:
        return "LITERAL_CHAR";
    case TokenType::IDENTIFIER:
        return "IDENTIFIER";
    case TokenType::KW_NIL:
        return "KW_NIL";
    case TokenType::KW_TRUE:
        return "KW_TRUE";
    case TokenType::KW_FALSE:
        return "KW_FALSE";
    case TokenType::KW_IF:
        return "KW_IF";
    case TokenType::KW_ELSE:
        return "KW_ELSE";
    case TokenType::KW_LOOP:
        return "KW_LOOP";
    case TokenType::KW_MATCH:
        return "KW_MATCH";
    case TokenType::KW_CASE:
        return "KW_CASE";
    case TokenType::KW_BREAK:
        return "KW_BREAK";
    case TokenType::KW_CONTINUE:
        return "KW_CONTINUE";
    case TokenType::KW_RETURN:
        return "KW_RETURN";
    case TokenType::KW_FOR:
        return "KW_FOR";
    case TokenType::KW_VAR:
        return "KW_VAR";
    case TokenType::KW_FUNC:
        return "KW_FUNC";
    case TokenType::KW_CONST:
        return "KW_CONST";
    case TokenType::KW_INT:
        return "KW_INT";
    case TokenType::KW_FLOAT:
        return "KW_FLOAT";
    case TokenType::KW_STRING:
        return "KW_STRING";
    case TokenType::KW_BOOL:
        return "KW_BOOL";
    case TokenType::KW_VOID:
        return "KW_VOID";
    case TokenType::KW_ANY:
        return "KW_ANY";
    case TokenType::KW_TYPE:
        return "KW_TYPE";
    case TokenType::KW_INTERFACE:
        return "KW_INTERFACE";
    case TokenType::KW_IMPL:
        return "KW_IMPL";
    case TokenType::KW_WILDCARD:
        return "KW_WILDCARD";
    case TokenType::KW_FLOW:
        return "KW_FLOW";
    case TokenType::KW_AWAIT:
        return "KW_AWAIT";
    case TokenType::KW_NOCK:
        return "KW_NOCK";
    case TokenType::KW_ASYNC:
        return "KW_ASYNC";
    case TokenType::KW_SYSTEM:
        return "KW_SYSTEM";
    case TokenType::KW_COMPONENT:
        return "KW_COMPONENT";
    case TokenType::KW_TARGET:
        return "KW_TARGET";
    case TokenType::KW_TAG:
        return "KW_TAG";
    case TokenType::KW_TAGGROUP:
        return "KW_TAGGROUP";
    case TokenType::KW_EXCLUSIVE:
        return "KW_EXCLUSIVE";
    case TokenType::KW_SET:
        return "KW_SET";
    case TokenType::KW_UNSET:
        return "KW_UNSET";
    case TokenType::KW_KITS:
        return "KW_KITS";
    case TokenType::KW_WITH:
        return "KW_WITH";
    case TokenType::KW_READ:
        return "KW_READ";
    case TokenType::KW_WRITE:
        return "KW_WRITE";
    case TokenType::KW_MODULE:
        return "KW_MODULE";
    case TokenType::KW_STRUCT:
        return "KW_STRUCT";
    case TokenType::KW_ENUM:
        return "KW_ENUM";
    case TokenType::TOKEN_EOF:
        return "TOKEN_EOF";
    case TokenType::TOKEN_ERROR:
        return "TOKEN_ERROR";
    case TokenType::COMMENT:
        return "COMMENT";
    }
    return "UNKNOWN_TOKEN";
}
/*画个token的数据结构图在这里。
        +-------------------+----+------+-------------+----+
token:  |start_offset:假设为1|line|column|length:假设为5|type|
        +-------------------+----+------+-------------+----+

        |←length→ |
        |---------|-+-+-+-+
string: |1|2|3|4|5|6|7|8|9|
        +↑--------+-+-+-+-+
         |start_offset
*/
} // namespace niki::syntax
