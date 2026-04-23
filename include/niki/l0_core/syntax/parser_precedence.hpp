#pragma once
#include "niki/l0_core/syntax/token.hpp"
#include <cstdint>

namespace niki::syntax {
// 在这里我们使用普拉特解析算法(Pratt Parsing Algorithm)来对表达式进行解析。
// 该算法创造性的使用符号优先级来对语法和节点进行聚合，使高优先级的节点自动成为低优先级节点的子节点，从而组成一颗语法树
// 构建过程是自顶向下递归下降，结果树中高优先级节点更深；后续求值/生成字节码常用后序遍历，才会表现出“先子后父”的效果
enum class Precedence : uint8_t {
    None,       // 0
    Or,         // ||
    And,        // &&
    BitOr,      // |
    BitXor,     // ^
    BitAnd,     // &
    Equality,   // == !=
    Comparison, // < > <= >=
    Shift,      // << >>
    Concat,     // ..
    Term,       // + -
    Factor,     // * / %
    DICE,       // d
    Unary,      // - !
    Call,       // () [] .
    Primary     // 标识符、数字、字符串、括号等
};

} // namespace niki::syntax
