#include "niki/semantic/type_checker.hpp"
#include "niki/syntax/token.hpp"

namespace niki::semantic {

NKType TypeChecker::checkExpression(syntax::ASTNodeIndex exprIdx) {
    auto [node, line, column] = getNodeCtx(exprIdx);
    NKType resultType = NKType::makeUnknown();

    switch (node.type) {
    case syntax::NodeType::BinaryExpr:
        resultType = checkBinaryExpr(exprIdx);
        break;
    case syntax::NodeType::LogicalExpr:
        resultType = checkLogicalExpr(exprIdx);
        break;
    case syntax::NodeType::UnaryExpr:
        resultType = checkUnaryExpr(exprIdx);
        break;
    case syntax::NodeType::LiteralExpr:
        resultType = checkLiteralExpr(exprIdx);
        break;
    case syntax::NodeType::IdentifierExpr:
        resultType = checkIdentifierExpr(exprIdx);
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
    case syntax::NodeType::CallExpr:
        resultType = checkCallExpr(exprIdx);
        break;
    case syntax::NodeType::MemberExpr:
        resultType = checkMemberExpr(exprIdx);
        break;
    case syntax::NodeType::DispatchExpr:
        resultType = checkDispatchExpr(exprIdx);
        break;
    case syntax::NodeType::ClosureExpr:
        resultType = checkClosureExpr(exprIdx);
        break;
    case syntax::NodeType::AwaitExpr:
        resultType = checkAwaitExpr(exprIdx);
        break;
    case syntax::NodeType::BorrowExpr:
        resultType = checkBorrowExpr(exprIdx);
        break;
    case syntax::NodeType::WildcardExpr:
        resultType = checkWildcardExpr(exprIdx);
        break;
    case syntax::NodeType::ImplicitCastExpr:
        resultType = checkImplicitCastExpr(exprIdx);
        break;
    default:
        break;
    }

    typeTable[exprIdx.index] = resultType;
    return resultType;
}

// ... [Existing implementations] ...

NKType TypeChecker::checkLiteralExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
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
    auto [node, line, column] = getNodeCtx(nodeIdx);

    NKType type = resolveSymbol(node.payload.identifier.name_id, line, column);
    // If not found, it might be a future global or built-in, but for MVP let's return Unknown instead of failing hard.
    return type;
}

NKType TypeChecker::checkBinaryExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    checkExpression(node.payload.binary.left);
    checkExpression(node.payload.binary.right);
    return NKType::makeInt();
}

NKType TypeChecker::checkArrayExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    auto elements = currentPool->get_list(node.payload.list.elements);
    for (auto el : elements) {
        checkExpression(el);
    }
    return NKType(NKBaseType::Array, -1);
}

NKType TypeChecker::checkMapExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
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
    auto [node, line, column] = getNodeCtx(nodeIdx);
    checkExpression(node.payload.index.target);
    checkExpression(node.payload.index.index);
    return NKType::makeUnknown();
}

NKType TypeChecker::checkLogicalExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    checkExpression(node.payload.logical.left);
    checkExpression(node.payload.logical.right);
    return NKType::makeBool();
}

NKType TypeChecker::checkUnaryExpr(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = currentPool->getNode(nodeIdx);
    checkExpression(node.payload.unary.operand);

    return NKType::makeUnknown();
}
NKType TypeChecker::checkCallExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkMemberExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkDispatchExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkClosureExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkAwaitExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkBorrowExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkWildcardExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkImplicitCastExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }

} // namespace niki::semantic