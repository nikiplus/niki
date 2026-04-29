#include "niki/l0_core/semantic/type_checker.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace niki::semantic {

/**
 * @brief 旧版入口（无全局语义上下文）已禁用。
 * @return 始终返回错误，提示调用方使用带全局上下文的重载。
 */
std::expected<TypeCheckResult, niki::diagnostic::DiagnosticBag> TypeChecker::check(syntax::ASTPool &pool,
                                                                                   syntax::ASTNodeIndex root) {
    niki::diagnostic::DiagnosticBag diagnostics;
    diagnostics.error(niki::diagnostic::events::SemanticCode::GenericError,
                      "Legacy TypeChecker entry is disabled; use global semantic context.");
    return std::unexpected(std::move(diagnostics));
}

/**
 * @brief 类型检查入口（带全局符号表/类型池）。
 * @param pool AST 池。
 * @param root 根节点索引。
 * @param global_symbols 全局符号表。
 * @param global_arena 全局类型池。
 * @return 成功返回空结果，失败返回诊断集合。
 */
std::expected<TypeCheckResult, niki::diagnostic::DiagnosticBag> TypeChecker::check(
    syntax::ASTPool &pool, syntax::ASTNodeIndex root, const niki::GlobalSymbolTable &global_symbols,
    const niki::GlobalTypeArena &global_arena) {
    currentPool = &pool;
    diagnostics = niki::diagnostic::DiagnosticBag{};
    symbols.clear();
    currentDepth = 0;
    inFunction = false;

    globalSymbols = &global_symbols;
    globalArena = &global_arena;
    visibleSymbols = nullptr;

    // 1. node_types 已经在 ASTPool 分配时预填充了 Unknown，
    // 我们不需要再做初始化操作，直接开始遍历覆盖它即可。

    // 2. 开始遍历
    checkNode(root);
    // 清理上下文，避免悬挂引用
    currentPool = nullptr;
    globalSymbols = nullptr;
    globalArena = nullptr;
    visibleSymbols = nullptr;

    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }

    return TypeCheckResult{};
}

/**
 * @brief 类型检查入口（带单元可见符号上下文）。
 */
std::expected<TypeCheckResult, niki::diagnostic::DiagnosticBag> TypeChecker::check(
    syntax::ASTPool &pool, syntax::ASTNodeIndex root, const niki::GlobalSymbolTable &global_symbols,
    const niki::GlobalTypeArena &global_arena, const UnitVisibleSymbols &visible_symbols) {
    currentPool = &pool;
    diagnostics = niki::diagnostic::DiagnosticBag{};
    symbols.clear();
    currentDepth = 0;
    inFunction = false;

    globalSymbols = &global_symbols;
    globalArena = &global_arena;
    visibleSymbols = &visible_symbols;

    checkNode(root);

    currentPool = nullptr;
    globalSymbols = nullptr;
    globalArena = nullptr;
    visibleSymbols = nullptr;

    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return TypeCheckResult{};
}

/** @brief 结束当前作用域并回收本层局部符号。 */
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

/**
 * @brief 在当前作用域声明符号。
 * @param name_id 符号名 id。
 * @param type 符号类型。
 * @param line 行号。
 * @param column 列号。
 * @param is_owned 是否拥有所有权。
 */
void TypeChecker::declareSymbol(uint32_t name_id, NKType type, uint32_t line, uint32_t column, bool is_owned) {
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

    // 2. 把新变量的名字、它的类型以及所有权状态，推入符号栈
    symbols.push_back({name_id, type, currentDepth, is_owned, false});
}

/**
 * @brief 解析符号类型（局部 -> 全局 -> 可见导入）。
 */
NKType TypeChecker::resolveSymbol(uint32_t name_id, uint32_t line, uint32_t column) {
    // 从栈顶（从后往前）开始查
    for (int i = static_cast<int>(symbols.size()) - 1; i >= 0; i--) {
        if (symbols[i].name_id == name_id) {
            return symbols[i].type;
        }
    }

    if (const auto *global_sym = globalSymbols->find(name_id); global_sym != nullptr) {
        return global_sym->type;
    }
    if (visibleSymbols != nullptr) {
        auto it = visibleSymbols->tables.find(name_id);
        if (it != visibleSymbols->tables.end()) {
            return it->second.type;
        }
    }

    reportError(line, column, "Undeclared variable.");
    return NKType::makeUnknown();
}

/** @brief 解析类型标注节点为 NKType。 */
NKType TypeChecker::resolveTypeAnnotation(syntax::ASTNodeIndex typeNodeIdx) {
    if (!typeNodeIdx.isvalid()) {
        return NKType::makeUnknown();
    }

    auto [node, line, column] = getNodeCtx(typeNodeIdx);

    // 如果是专门的 TypeExpr 节点
    if (node.type == syntax::NodeType::TypeExpr) {
        switch (node.payload.type_expr.base_type) {
        case syntax::TokenType::KW_INT:
            return NKType::makeInt();
        case syntax::TokenType::KW_FLOAT:
            return NKType::makeFloat();
        case syntax::TokenType::KW_BOOL:
            return NKType::makeBool();
        case syntax::TokenType::KW_STRING:
            return NKType(NKBaseType::String, -1);
        default:
            reportError(line, column, "Unknown built-in type annotation.");
            return NKType::makeUnknown();
        }
    }

    // 兼容退化的 IdentifierExpr (比如用户自定义结构体或首字母大写的 Int)
    if (node.type == syntax::NodeType::IdentifierExpr) {
        uint32_t name_id = node.payload.identifier.name_id;

        // --- 极速 O(1) 整数比对 ---
        if (name_id == currentPool->ID_INT)
            return NKType::makeInt();
        if (name_id == currentPool->ID_FLOAT)
            return NKType::makeFloat();
        if (name_id == currentPool->ID_BOOL)
            return NKType::makeBool();
        if (name_id == currentPool->ID_STRING)
            return NKType(NKBaseType::String, -1);

        // 1) 先查全局符号表（同模块/已预声明的顶层类型别名/结构体/函数签名）
        if (const auto *global_sym = globalSymbols->find(name_id); global_sym != nullptr) {
            if (global_sym->kind == niki::Kind::Struct || global_sym->kind == niki::Kind::Function ||
                global_sym->kind == niki::Kind::TypeAlias) {
                return global_sym->type;
            }
        }

        // 2) 再查显式导入可见表（用于跨模块类型别名解析）
        if (visibleSymbols != nullptr) {
            auto it = visibleSymbols->tables.find(name_id);
            if (it != visibleSymbols->tables.end()) {
                const auto &sym = it->second;
                if (sym.kind == niki::Kind::Struct || sym.kind == niki::Kind::Function || sym.kind == niki::Kind::TypeAlias) {
                    return sym.type;
                }
            }
        }

        std::string_view typeName = currentPool->getStringId(name_id);
        reportError(line, column, "Unknown type name :" + std::string(typeName));
        return NKType::makeUnknown();
    }

    reportError(line, column, "Invalid type annotation. Expected a type or identifier.");
    return NKType::makeUnknown();
};

/** @brief 上报语义错误。 */
void TypeChecker::reportError(uint32_t line, uint32_t column, const std::string &message) {
    diagnostics.error(niki::diagnostic::events::SemanticCode::GenericError, message,
                      niki::diagnostic::makeSourceSpan(currentPool != nullptr ? currentPool->source_path : "", line, column));
}

/**
 * @brief 语义总分发：表达式/语句/声明三路。
 * @param nodeIdx 节点索引。
 * @return 若为表达式则返回其类型，否则返回 Unknown。
 */
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