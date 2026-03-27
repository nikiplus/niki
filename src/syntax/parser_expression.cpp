#include "niki/syntax/ast.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/parser_precedence.hpp"
#include "niki/syntax/token.hpp"
#include <vector>
using namespace niki::syntax;

ASTNodeIndex Parser::parseExpression(Precedence precedence) {
    ASTNodeIndex left = parsePrefix(previous.type);
    while (precedence < getPrecedence(current.type)) {
        advance();
        left = parseInfix(previous.type, left);
    }
    return left;
};

ASTNodeIndex Parser::parsePrefix(TokenType type) {
    ASTNodePayload payload{};
    switch (type) {
        //---字面量与标识符---
    case TokenType::LITERAL_INT:
    case TokenType::LITERAL_FLOAT:
    case TokenType::LITERAL_STRING:
    case TokenType::LITERAL_CHAR:
    case TokenType::KEYWORD_TRUE:
    case TokenType::KEYWORD_FALSE:
    case TokenType::NIL:
        return emitNode(NodeType::LiteralExpr, payload);
    case TokenType::IDENTIFIER:
        return emitNode(NodeType::IdentifierExpr, payload);

        //---一元前缀运算符
    case TokenType::SYM_MINUS:
    case TokenType::SYM_BANG:
    case TokenType::SYM_BIT_NOT: {
        TokenType opType = type;
        Token startToken = previous;
        ASTNodeIndex operand = parseExpression(Precedence::Unary);
        payload.unary.op = opType;
        payload.unary.operand = operand;
        return emitNode(NodeType::UnaryExpr, payload, startToken);
    }

    //---括号---
    // 因为只有解析值时程序才会走这条路径，因此这里的括号和语句解析中的括号并不构成冲突。
    case TokenType::SYM_PAREN_L: {
        ASTNodeIndex expr = parseExpression(Precedence::None);
        consume(TokenType::SYM_PAREN_R, "Expected')'after expression.");
        return expr;
    }
    case TokenType::SYM_BRACKET_L: {
        Token startToken = previous;
        std::vector<ASTNodeIndex> elements;

        if (current.type != TokenType::SYM_BRACKET_R) {
            do {
                if (current.type == TokenType::SYM_BRACKET_R)
                    break;
                elements.push_back(parseExpression(Precedence::None));
            } while (match(TokenType::SYM_COMMA));
        }
        consume(TokenType::SYM_BRACKET_R, "Expected']'at end of array.");
        payload.array.elements = astPool.allocateList(elements);
        return emitNode(NodeType::ArrayExpr, payload, startToken);
    }
    // 在parse期，我们不对传入map的数据类型进行检测。
    // 但在语义分析器，我们必须要遍历mapexpr的所有key，检查它们的类型，如果它们的类型没有实现hashalbe接口(比如包含了可变数组和闭包)，立刻抛出编译错误。
    case TokenType::SYM_BRACE_L: {
        Token startToken = previous;
        std::vector<ASTNodeIndex> Interleaved;
        if (current.type != TokenType::SYM_BRACE_R) {
            do {
                if (current.type == TokenType::SYM_BRACE_R)
                    break;
                ASTNodeIndex key = parseExpression(Precedence::None);
                Interleaved.push_back(key);
                consume(TokenType::SYM_COLON, "Expected':'after map key.");
                ASTNodeIndex value = parseExpression(Precedence::None);
                Interleaved.push_back(value);
            } while (match(TokenType::SYM_COMMA));
        }
        consume(TokenType::SYM_BRACE_R, "Expected '}'after map elements.");
        payload.map.entries = astPool.allocateList(Interleaved);
        return emitNode(NodeType::MapExpr, payload, startToken);
    }
    default:
        errorAtCurrent("Expected expression.");
        return emitNode(NodeType::ErrorNode, payload);
    }
};

ASTNodeIndex Parser::parseInfix(TokenType type, ASTNodeIndex left) {
    ASTNodePayload payload{};
    Token startToken = previous;
    switch (type) {
    case TokenType::SYM_PLUS:
    case TokenType::SYM_MINUS:
    case TokenType::SYM_STAR:
    case TokenType::SYM_SLASH:
    case TokenType::SYM_MOD:
    case TokenType::SYM_EQUAL_EQUAL:
    case TokenType::SYM_BANG_EQUAL:
    case TokenType::SYM_GREATER:
    case TokenType::SYM_GREATER_EQUAL:
    case TokenType::SYM_LESS:
    case TokenType::SYM_LESS_EQUAL:
    case TokenType::SYM_AND:
    case TokenType::SYM_OR: {
        payload.binary.op = type;
        payload.binary.left = left;
        Precedence precedence = getPrecedence(type);

        payload.binary.right = parseExpression(precedence);
        return emitNode(NodeType::BinaryExpr, payload, startToken);
    }
    }
};

Precedence Parser::getPrecedence(TokenType type) const {
    switch (type) {
    case TokenType::SYM_OR:
        return Precedence::Or;
    case TokenType::SYM_AND:
        return Precedence::And;

    case TokenType::SYM_EQUAL_EQUAL:
    case TokenType::SYM_BANG_EQUAL:
        return Precedence::Equality;

    case TokenType::SYM_GREATER:
    case TokenType::SYM_GREATER_EQUAL:
    case TokenType::SYM_LESS:
    case TokenType::SYM_LESS_EQUAL:
        return Precedence::Comparison;

    case TokenType::SYM_PLUS:
    case TokenType::SYM_MINUS:
        return Precedence::Term;

    case TokenType::SYM_STAR:
    case TokenType::SYM_SLASH:
    case TokenType::SYM_MOD:
        return Precedence::Factor;

    case TokenType::SYM_DOT:
    case TokenType::SYM_PAREN_L:   // 函数调用 foo()
    case TokenType::SYM_BRACKET_L: // 数组索引 arr[0]
        return Precedence::Call;

    default:
        return Precedence::None;
    }
}