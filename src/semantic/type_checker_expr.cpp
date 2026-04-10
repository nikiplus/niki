#include "niki/semantic/type_checker.hpp"
#include "niki/syntax/token.hpp"

namespace niki::semantic {

NKType TypeChecker::checkExpression(syntax::ASTNodeIndex exprIdx) {
    const auto &node = currentPool->getNode(exprIdx);
    NKType resultType = NKType::makeUnknown();

    switch (node.type) {
    case syntax::NodeType::LiteralExpr:
        resultType = checkLiteralExpr(exprIdx);
        break;
    case syntax::NodeType::IdentifierExpr:
        resultType = checkIdentifierExpr(exprIdx);
        break;
    case syntax::NodeType::BinaryExpr:
        resultType = checkBinaryExpr(exprIdx);
        break;
    case syntax::NodeType::ArrayExpr:
        resultType = checkArrayExpr(exprIdx);
        break;
    case syntax::NodeType::MapExpr:
        resultType = checkMapExpr(exprIdx);
        break;
    case syntax::NodeType::IndexExpr:
        resultType = checkIndexExpr(exprIdx);
        break;
    default:
        break;
    }

    typeTable[exprIdx.index] = resultType;
    return resultType;
}

NKType TypeChecker::checkLiteralExpr(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = currentPool->getNode(nodeIdx);
    switch (node.payload.literal.literal_type) {
    case syntax::TokenType::LITERAL_INT:
        return NKType::makeInt();
    case syntax::TokenType::LITERAL_FLOAT:
        return NKType::makeFloat();
    case syntax::TokenType::KW_TRUE:
    case syntax::TokenType::KW_FALSE:
        return NKType::makeBool();
    case syntax::TokenType::LITERAL_STRING:
        return NKType(NKBaseType::String, -1);
    default:
        return NKType::makeUnknown();
    }
}

NKType TypeChecker::checkIdentifierExpr(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;
    return resolveSymbol(node.payload.identifier.name_id, line, column);
}

NKType TypeChecker::checkBinaryExpr(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = currentPool->getNode(nodeIdx);
    checkExpression(node.payload.binary.left);
    checkExpression(node.payload.binary.right);
    return NKType::makeInt();
}

NKType TypeChecker::checkArrayExpr(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = currentPool->getNode(nodeIdx);
    auto elements = currentPool->get_list(node.payload.list.elements);
    for (auto el : elements) {
        checkExpression(el);
    }
    return NKType(NKBaseType::Array, -1);
}

NKType TypeChecker::checkMapExpr(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = currentPool->getNode(nodeIdx);
    const auto &map_data = currentPool->map_data[node.payload.map.map_data_index];
    auto keys = currentPool->get_list(map_data.keys);
    auto values = currentPool->get_list(map_data.values);

    for (size_t i = 0; i < keys.size(); ++i) {
        checkExpression(keys[i]);
        checkExpression(values[i]);
    }
    return NKType(NKBaseType::Map, -1);
}

NKType TypeChecker::checkIndexExpr(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = currentPool->getNode(nodeIdx);
    checkExpression(node.payload.index.target);
    checkExpression(node.payload.index.index);
    return NKType::makeUnknown();
}

} // namespace niki::semantic