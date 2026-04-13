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
    // 【符号解析（类型查询）】
    // 当遇到 a + 1 里的 'a' 时调用。我们需要知道 'a' 是什么类型。

    // 从栈顶（从后往前）开始查。为什么从后往前？
    // 因为如果有两层作用域 { var a = 1; { var a = "str"; print(a); } }
    // 我们希望内层的 a (String) 遮蔽（Shadow）外层的 a (Int)。从后往前刚好先碰到内层的 a。
    for (int i = static_cast<int>(symbols.size()) - 1; i >= 0; i--) {
        if (symbols[i].name_id == name_id) {
            return symbols[i].type;
        }
    }

    // TODO: For MVP, we do not report error here because global variables or built-ins
    // might not be properly populated in this local symbol table yet.
    // We just return Unknown and let it pass.
    // 如果找不到，理论上应该报错 "Undeclared
    // variable."。但为了兼容我们现存的顶层测试代码（平铺的隐式全局变量），暂且放过，返回 Unknown。
    return NKType::makeUnknown();
}

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