#include "niki/semantic/type_checker.hpp"
#include "niki/semantic/nktype.hpp"
#include "niki/syntax/ast.hpp"
#include <cstdint>
#include <string>
#include <string_view>

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
    // 【作用域退出（栈回退）】
    // 当遇到 } 或者执行完一个代码块时调用。
    // 我们把符号表看作一个栈，从后往前找，只要符号的 depth 等于当前深度，就说明它是这个代码块里的局部变量，
    // 出了代码块就死了，必须把它从栈顶弹出去（销毁）。
    while (!symbols.empty() && symbols.back().depth == currentDepth) {
        symbols.pop_back();
    }
    // 退出后，当前作用域深度减一
    currentDepth--;
}

void TypeChecker::declareSymbol(uint32_t name_id, NKType type, uint32_t line, uint32_t column) {
    // 【变量声明登记】
    // 当遇到 var a = 10; 这样的语句时调用。

    // 1. 检查同名冲突：在当前大括号（作用域）内，不允许声明两个名字一模一样的变量。
    // 我们只查当前 depth，如果查到了外层的同名变量（depth < currentDepth），直接 break（允许内层覆盖外层，这叫
    // Shadowing）。
    for (int i = static_cast<int>(symbols.size()) - 1; i >= 0; i--) {
        if (symbols[i].depth < currentDepth)
            break;
        if (symbols[i].name_id == name_id) {
            reportError(line, column, "Variable already declared in this scope.");
            return;
        }
    }

    // 2. 把新变量的名字和它的类型，推入符号栈
    symbols.push_back({name_id, type, currentDepth});
}

NKType TypeChecker::resolveSymbol(uint32_t name_id, uint32_t line, uint32_t column) {
    // 从栈顶（从后往前）开始查
    for (int i = static_cast<int>(symbols.size()) - 1; i >= 0; i--) {
        if (symbols[i].name_id == name_id) {
            return symbols[i].type;
        }
    }

    // 既然我们已经禁用了全局野变量，且所有的顶层 func 都被注册到了 symbols 的 depth 0，
    // 那么在这里如果还找不到，就说明它绝对是一个未声明的变量或拼写错误！
    // 必须立刻报错，绝不妥协！
    reportError(line, column, "Undeclared variable.");
    return NKType::makeUnknown();
}

NKType TypeChecker::resolveTypeAnnotation(syntax::ASTNodeIndex typeNodeIdx) {
    if (!typeNodeIdx.isvalid()) {
        return NKType::makeUnknown();
    }

    auto [node, line, column] = getNodeCtx(typeNodeIdx);

    // 目前类型标注只能是标识符（比如 Int, String, Player）
    // 暂不支持复杂的泛型如 Array<Int>，那是后话。
    if (node.type != syntax::NodeType::IdentifierExpr) {
        reportError(line, column, "Invalid type annotation.Expected an identifier.");
        return NKType::makeUnknown();
    }

    uint32_t name_id = node.payload.identifier.name_id;

    // --- 极速 O(1) 整数比对 ---
    if (name_id == currentPool->ID_INT) return NKType::makeInt();
    if (name_id == currentPool->ID_FLOAT) return NKType::makeFloat();
    if (name_id == currentPool->ID_BOOL) return NKType::makeBool();
    if (name_id == currentPool->ID_STRING) return NKType(NKBaseType::String, -1);

    // 查找用户自定义类型（mvp只返回unknow或占位，未来在实现查找struct）

    // 如果都不认识，才去取字符串用于报错
    std::string_view typeName = currentPool->getStringId(name_id);
    reportError(line, column, "Unknown type name :" + std::string(typeName));
    return NKType::makeUnknown();
};

void TypeChecker::reportError(uint32_t line, uint32_t column, const std::string &message) {
    errors.push_back({line, column, message});
}

NKType TypeChecker::checkNode(syntax::ASTNodeIndex nodeIdx) {
    if (!nodeIdx.isvalid())
        return NKType::makeUnknown();

    const auto &node = getNodeCtx(nodeIdx).node;

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