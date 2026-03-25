#pragma once

#include <cstdint>

namespace niki::syntax {
// 在这里我们使用普拉特解析算法(Pratt Parsing Algorithm)来对表达式进行解析。
// 该算法创造性的使用符号优先级来对语法和节点进行聚合，使高优先级的节点自动成为低优先级节点的子节点，从而组成一颗语法树
// 构建过程是自顶向下递归下降，结果树中高优先级节点更深；后续求值/生成字节码常用后序遍历，才会表现出“先子后父”的效果
enum class Precedence : uint8_t {
    None,       // 0
    Assignment, // =
    Or,         // ||
    And,        // &&
    Equality,   // == !=
    Comparison, // < > <= >=
    Term,       // + -
    Factor,     // * / %
    Unary,      // - !
    Call,       // () [] .
    Primary     // 标识符、数字、字符串、括号等
};

/** @note 为了详细解释普拉特算法，我将在这里进行一个图的绘制。
*首先假设我们面对这样一段公式
*@code: a = 1*2+3
*经过我们的scanner之后，这一串字符被拆解为token，并在compiler所持有的vector<Token>中以以下格式存储。
*注意，我们这里用LITERAL_INT来标记1/2/3等数字，而不是KEYWORD_INT,这是因为类型！=值本身。
*首先，为了能进行扫描和判断，我们需要创建两个指针。
*previous/current
*然后我们来写这样一段代码——也是实际的解析器代码

ASTNodeIndex Parser::parseExpression(Precedence precedence) {
    ASTNodeIndex left = parsePrefix(previous.type);
    while (precedence < getPrecedence(current.type)) {
        advance();
        left = parseInfix(previous.type, left);
    }
    return left;
};
*先不用管代码，先跟着我往下思考。
*为了能获取vector<Token>中的tokens，我们首先需要一对指针——一个用于获取”曾经“的值，一个用于获取”未来“的值。

*
*初始化指针:（P）previous=current；（C）current = tokens[tokenIndex+1]
*         |P↓           |C↓         |
*@strack: |LITERAL_STR:a|SYM_EQUAL:=|LITERAL_INT:1|SYM_STAR：*|LITERAL_INT:2|SYM_PLUS:+|LITERAL_INT:3|





*/

} // namespace niki::syntax
