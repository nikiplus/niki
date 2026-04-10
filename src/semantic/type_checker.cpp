#include "niki/semantic/type_checker.hpp"

namespace niki::semantic {

std::expected<TypeCheckResult, TypeCheckErrorResult> TypeChecker::check(const syntax::ASTPool &pool,
                                                                        syntax::ASTNodeIndex root) {
    currentPool = &pool;
    errors.clear();
    symbols.clear();
    currentDepth = 0;

    // 1. 初始化类型表
    typeTable.resize(pool.nodes.size(), NKType::makeUnknown());

    // 2. 开始遍历
    checkNode(root);

    currentPool = nullptr;

    if (!errors.empty()) {
        return std::unexpected(TypeCheckErrorResult{std::move(errors)});
    }

    return TypeCheckResult{std::move(typeTable)};
}

void TypeChecker::endScope() {
    while (!symbols.empty() && symbols.back().depth == currentDepth) {
        symbols.pop_back();
    }
    currentDepth--;
}

void TypeChecker::declareSymbol(uint32_t name_id, NKType type, uint32_t line, uint32_t column) {
    for (int i = static_cast<int>(symbols.size()) - 1; i >= 0; i--) {
        if (symbols[i].depth < currentDepth)
            break;
        if (symbols[i].name_id == name_id) {
            reportError(line, column, "Variable already declared in this scope.");
            return;
        }
    }
    symbols.push_back({name_id, type, currentDepth});
}

NKType TypeChecker::resolveSymbol(uint32_t name_id, uint32_t line, uint32_t column) {
    for (int i = static_cast<int>(symbols.size()) - 1; i >= 0; i--) {
        if (symbols[i].name_id == name_id) {
            return symbols[i].type;
        }
    }
    // TODO: For MVP, we do not report error here because global variables or built-ins
    // might not be properly populated in this local symbol table yet.
    // We just return Unknown and let it pass.
    // reportError(line, column, "Undeclared variable.");
    return NKType::makeUnknown();
}

void TypeChecker::reportError(uint32_t line, uint32_t column, const std::string &message) {
    errors.push_back({line, column, message});
}

NKType TypeChecker::checkNode(syntax::ASTNodeIndex nodeIdx) {
    if (!nodeIdx.isvalid())
        return NKType::makeUnknown();

    const auto &node = currentPool->getNode(nodeIdx);

    if (node.type >= syntax::NodeType::BinaryExpr && node.type <= syntax::NodeType::ImplicitCastExpr) {
        return checkExpression(nodeIdx);
    } else if (node.type >= syntax::NodeType::ExpressionStmt && node.type <= syntax::NodeType::DetachStmt) {
        checkStatement(nodeIdx);
        return NKType::makeUnknown();
    } else {
        checkDeclaration(nodeIdx);
        return NKType::makeUnknown();
    }
}

} // namespace niki::semantic