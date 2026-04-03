#pragma once
#include <cstdint>
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
    NIL,            // nil

    /*---[identifier]标识符---*/
    IDENTIFIER, // 标识符

    /*---[keyword]关键字---*/
    // 控制流
    KEYWORD_TRUE,     // true 真
    KEYWORD_FALSE,    // false 假
    KEYWORD_IF,       // if 如果
    KEYWORD_ELSE,     // else 否则
    KEYWORD_LOOP,     // loop 循环
    KEYWORD_MATCH,    // match 匹配
    KEYWORD_BREAK,    // break 跳出循环
    KEYWORD_CONTINUE, // continue 继续循环
    KEYWORD_RETURN,   // return 返回值
    // 变量与函数
    KEYWORD_VAR,   // var
    KEYWORD_FUNC,  // func
    KEYWORD_CONST, // const
    // 类型系统 (静态类型检查基础设施)
    KEYWORD_INT,    // int
    KEYWORD_FLOAT,  // float
    KEYWORD_STRING, // string
    KEYWORD_BOOL,   // bool
    KEYWORD_VOID,   // void
    KEYWORD_ANY,    // any (逃生舱)
    KEYWORD_TYPE,   // type (类型别名)

    KEYWORD_INTERFACE, // interface (接口)
    KEYWORD_IMPL,      // impl (实现)

    /*---[control]控制标记---*/
    TOKEN_EOF,   // 结束标记
    TOKEN_ERROR, // 错误标记

    /*---[advanced]高级特性---*/
    // 逻辑编排与挂起
    NK_FLOW,       // flow 声明一个流程
    NK_FLOW_AWAIT, // await 挂起并等待某结果
    NK_FLOW_NOCK,  // nock 主动放弃当前帧执行权
    NK_FLOW_ASYNC, // async 异步函数
    // 系统拓扑与并发
    NK_SYSTEM,    // system 系统
    NK_COMPONENT, // component 组件
    // 查询与检索
    NK_TARGET,    // target 目标
    NK_TAG,       // tag 标签
    NK_TAGGROUP,  // taggroup 标签组
    NK_EXCLUSIVE, // exclusive 互斥修饰符
    NK_SET,       // set 设置
    NK_UNSET,     // unset 取消设置
    // 环境与权限
    NK_KITS,  // kits
    NK_WITH,  // with 引入模块
    NK_READ,  // read 读取模块
    NK_WRITE, // write 写入模块
    // 结构
    NK_MODULE, // module (模块)
    NK_STRUCT, // struct (结构体)
    NK_ENUM,   // enum (枚举)

    // 其他
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
/*画个token的数据结构图在这里。
        +-------------------+----+------+-------------+----+
token:  |start_offset:假设为1|line|column|length:假设为5|type|
        +-------------------+----+------+-------------+----+

        |←length→ |
        |---------|-+-+-+-+
string: |1|2|3|4|5|6|7|8|9|
        +↑--------+-+-+-+-+
         |start_offest
*/
} // namespace niki::syntax
