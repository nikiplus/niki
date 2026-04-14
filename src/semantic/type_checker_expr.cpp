#include "niki/semantic/nktype.hpp"
#include "niki/semantic/type_checker.hpp"
#include "niki/syntax/ast.hpp"
#include "niki/syntax/token.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

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
    // 如果是空数组，目前只能返回 Unknown，等待未来的类型推导（比如 var a: Array<Int> = []）
    if (elements.empty()) {
        return NKType(NKBaseType::Array, -1);
    }

    // 获取第一个元素的类型作为基准类型
    NKType elementType = checkExpression(elements[0]);

    // 强制同构检查：后续所有元素的类型必须与第一个元素相同
    for (size_t i = 1; i < elements.size(); ++i) {
        NKType currentType = checkExpression(elements[i]);

        // 如果遇到 Unknown 我们暂时放过，但如果有明确冲突，立刻报错
        if (currentType.getBase() != NKBaseType::Unknown && elementType.getBase() != elementType.getBase()) {
            reportError(line, column,
                        "Heterogeneous arrays are not allowed. Expected element type " +
                            std::to_string((int)elementType.getBase()) + ", got" +
                            std::to_string((int)currentType.getBase()));
        }
    }
    return NKType(NKBaseType::Array, -1);
}

NKType TypeChecker::checkMapExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    const auto &map_data = currentPool->map_data[node.payload.map.map_data_index];
    auto keys = currentPool->get_list(map_data.keys);
    auto values = currentPool->get_list(map_data.values);

    if (keys.empty()) {
        return NKType(NKBaseType::Map, -1);
    }

    NKType keyType = checkExpression(keys[0]);
    NKType valueType = checkExpression(values[0]);

    for (size_t i = 1; i < keys.size(); ++i) {
        NKType curKeyType = checkExpression(keys[i]);
        NKType curValType = checkExpression(values[i]);

        if (curKeyType.getBase() != NKBaseType::Unknown && keyType.getBase() != NKBaseType::Unknown &&
            curKeyType != keyType) {
            reportError(line, column, "Map keys must have uniform type.");
        }
        if (curValType.getBase() != NKBaseType::Unknown && valueType.getBase() != NKBaseType::Unknown &&
            curValType != valueType) {
            reportError(line, column, "Map values must have uniform type.");
        }
    }
    return NKType(NKBaseType::Map, -1);
}

NKType TypeChecker::checkIndexExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    
    NKType targetType = checkExpression(node.payload.index.target);
    NKType indexType = checkExpression(node.payload.index.index);

    if (targetType.getBase() == NKBaseType::Unknown) {
        return NKType::makeUnknown(); // MVP 兜底
    }

    if (targetType.getBase() != NKBaseType::Array && targetType.getBase() != NKBaseType::Map) {
        reportError(line, column, "Cannot index a non-array/map type.");
        return NKType::makeUnknown();
    }

    // 在完整的泛型系统中，这里应该返回 targetType.elementType 或 targetType.valueType。
    // 但在 MVP 中，因为我们丢弃了泛型参数，我们只能退化返回 Unknown，
    // 让使用者在取回值之后承担弱类型的风险。
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
NKType TypeChecker::checkCallExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    // 检查被调用的对象。
    NKType calleeType = checkExpression(node.payload.call.callee);

    if (calleeType.getBase() == NKBaseType::Unknown) {
        // 兜底放过
        return NKType::makeUnknown();
    }

    if (calleeType.getBase() != NKBaseType::Function && calleeType.getBase() != NKBaseType::Unknown) {
        reportError(line, column, "Attempt to call a non-function value.");
        return NKType::makeUnknown();
    }

    // 提取签名进行比对
    uint32_t sig_id = calleeType.handle & 0x00FFFFFF; // 提取低24位
    const FunctionSignature &sig = const_cast<syntax::ASTPool *>(currentPool)->func_sigs[sig_id];

    // 2. 检查所有的参数表达式
    std::span<const syntax::ASTNodeIndex> argNodes = currentPool->get_list(node.payload.call.arguments);
    if (argNodes.size() != sig.param_types.size()) {
        reportError(line, column, "Argument count mismatch.");
    }

    // 参数类型校验
    for (size_t i = 0; i < argNodes.size(); ++i) {
        NKType argType = checkExpression(argNodes[i]);
        // if (i < sig.param_types.size() && argType != sig.param_types[i]) {
        //     reportError(... "Type mismatch in argument");
        // }
    }
    // 3. 返回值类型
    return sig.return_type;
}
NKType TypeChecker::checkMemberExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkDispatchExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkClosureExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkAwaitExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkBorrowExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkWildcardExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkImplicitCastExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }

} // namespace niki::semantic