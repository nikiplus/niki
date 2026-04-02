#include "niki/syntax/ast.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/parser_precedence.hpp"
#include "niki/syntax/token.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
using namespace niki::syntax;

ASTNodeIndex Parser::parseExpression(Precedence precedence) {
    advance();
    ASTNodeIndex left = parsePrefix(previous.type);
    while (precedence < getPrecedence(current.type)) {
        advance();
        left = parseInfix(previous.type, left);
    }
    return left;
};

ASTNodeIndex Parser::parsePrefix(TokenType type) {
    ASTNodePayload payload{};
    Token startToken = previous;
    switch (type) {
        //---字面量与标识符---
    case TokenType::LITERAL_INT: {
        std::string_view lexeme = source.substr(startToken.start_offset, startToken.length);
        int64_t numeric_val = std::stoll(std::string(lexeme));
        uint32_t const_idx = astPool.addConstant(vm::Value::makeInt(numeric_val));
        payload.literal.literal_type = TokenType::LITERAL_INT;
        payload.literal.const_pool_index = const_idx;
        return emitNode(NodeType::LiteralExpr, payload, startToken);
    }
    case TokenType::LITERAL_FLOAT: {
        std::string_view lexeme = source.substr(startToken.start_offset, startToken.length);
        int64_t numeric_val = std::stoll(std::string(lexeme));
        uint32_t const_idx = astPool.addConstant(vm::Value::makeFloat(numeric_val));
        payload.literal.literal_type = TokenType::LITERAL_FLOAT;
        payload.literal.const_pool_index = const_idx;
        return emitNode(NodeType::LiteralExpr, payload, startToken);
    }
    case TokenType::LITERAL_STRING:
    case TokenType::LITERAL_CHAR:
        // 目前内存设计尚未完成，字符串和字符暂用空对象，但需提前标明类型
        payload.literal.literal_type = type;
        payload.literal.const_pool_index = astPool.addConstant(vm::Value::makeNil());
        return emitNode(NodeType::LiteralExpr, payload, startToken);
    case TokenType::KEYWORD_TRUE: {
        uint32_t const_idx = astPool.addConstant(vm::Value::makeBool(true));
        payload.literal.literal_type = TokenType::KEYWORD_TRUE;
        payload.literal.const_pool_index = const_idx;
        return emitNode(NodeType::LiteralExpr, payload, startToken);
    }
    case TokenType::KEYWORD_FALSE: {
        uint32_t const_idx = astPool.addConstant(vm::Value::makeBool(false));
        payload.literal.literal_type = TokenType::KEYWORD_FALSE;
        payload.literal.const_pool_index = const_idx;
        return emitNode(NodeType::LiteralExpr, payload, startToken);
    }
    case TokenType::NIL: {
        uint32_t const_idx = astPool.addConstant(vm::Value::makeNil());
        payload.literal.literal_type = TokenType::NIL;
        payload.literal.const_pool_index = const_idx;
        return emitNode(NodeType::LiteralExpr, payload, startToken);
    }
    case TokenType::IDENTIFIER: {
        std::string_view name_view = source.substr(startToken.start_offset, startToken.length);

        uint32_t interned_name_id = astPool.internString(name_view);
        payload.identifier.name_id = interned_name_id;

        return emitNode(NodeType::IdentifierExpr, payload, startToken);
    }

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
    case TokenType::SYM_PAREN_L: {
        payload.call.callee = left;
        std::vector<ASTNodeIndex> args;
        if (current.type != TokenType::SYM_PAREN_R) {
            do {
                if (current.type == TokenType::SYM_PAREN_R)
                    break;

                args.push_back(parseExpression(Precedence::None));
            } while (match(TokenType::SYM_COMMA));
        }
        consume(TokenType::SYM_PAREN_R, "Expected')'after arguments.");
        payload.call.arguments = astPool.allocateList(args);
        return emitNode(NodeType::CallExpr, payload, startToken);
    }
    case TokenType::SYM_BRACKET_L: {
        payload.index.target = left;
        payload.index.index = parseExpression(Precedence::None);
        consume(TokenType::SYM_BRACKET_R, "Expected ']'after index.");
        return emitNode(NodeType::IndexExpr, payload, startToken);
    }
    case TokenType::SYM_DOT: {
        payload.member.object = left;
        consume(TokenType::IDENTIFIER, "Expected property name after'.'");
        std::string_view property_name = source.substr(previous.start_offset, previous.length);
        payload.member.property_id = astPool.internString(property_name);
        return emitNode(NodeType::MemberExpr, payload, startToken);
    }

    default:
        errorAtCurrent("Unexpected infix operator.");
        return emitNode(NodeType::ErrorNode, payload, startToken);
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