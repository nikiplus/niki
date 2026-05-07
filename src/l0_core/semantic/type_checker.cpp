#include "niki/l0_core/semantic/type_checker.hpp"
#include "niki/l0_core/semantic/module_namespace.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    syntax::ASTPool &pool, syntax::ASTNodeIndex root, const niki::TypeArena &global_arena,
    ModuleId module_id, const ModuleNamespace &module_namespace) {
    currentPool = &pool;
    diagnostics = niki::diagnostic::DiagnosticBag{};
    symbols.clear();
    currentDepth = 0;
    inFunction = false;
    loopNestingDepth = 0;

    typeArena = &global_arena;
    visibleSymbols = nullptr;
    currentModuleId = module_id;
    moduleNamespace = &module_namespace;

    // 1. node_types 已经在 ASTPool 分配时预填充了 Unknown，
    // 我们不需要再做初始化操作，直接开始遍历覆盖它即可。

    // 2. 开始遍历
    checkNode(root);
    // 清理上下文，避免悬挂引用
    currentPool = nullptr;
    typeArena = nullptr;
    visibleSymbols = nullptr;
    currentModuleId = kInvalidModuleId;
    moduleNamespace = nullptr;

    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }

    return TypeCheckResult{};
}

/**
 * @brief 类型检查入口（带单元可见符号上下文）。
 */
std::expected<TypeCheckResult, niki::diagnostic::DiagnosticBag> TypeChecker::check(
    syntax::ASTPool &pool, syntax::ASTNodeIndex root, const niki::TypeArena &global_arena,
    const UnitVisibleSymbols &visible_symbols, ModuleId module_id, const ModuleNamespace &module_namespace) {
    currentPool = &pool;
    diagnostics = niki::diagnostic::DiagnosticBag{};
    symbols.clear();
    currentDepth = 0;
    inFunction = false;
    loopNestingDepth = 0;

    typeArena = &global_arena;
    visibleSymbols = &visible_symbols;
    currentModuleId = module_id;
    moduleNamespace = &module_namespace;

    checkNode(root);

    currentPool = nullptr;
    typeArena = nullptr;
    visibleSymbols = nullptr;
    currentModuleId = kInvalidModuleId;
    moduleNamespace = nullptr;

    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return TypeCheckResult{};
}

bool TypeChecker::isHeapType(NKType type) {
    switch (type.getBase()) {
    case NKBaseType::Array:
    case NKBaseType::Map:
    case NKBaseType::Object:
    case NKBaseType::String:
        return true;
    default:
        return false;
    }
}

void TypeChecker::tryMarkRhsIdentifierMovedForAssign(uint32_t rhs_name_id) {
    for (int i = static_cast<int>(symbols.size()) - 1; i >= 0; --i) {
        if (symbols[i].depth < currentDepth) {
            break;
        }
        if (symbols[i].name_id != rhs_name_id) {
            continue;
        }
        if (symbols[i].is_owned && isHeapType(symbols[i].type) && !symbols[i].is_moved) {
            symbols[i].is_moved = true;
        }
        break;
    }
}

/** @brief 弹出当前作用域：可选记录本层待 OP_FREE 的 owned 符号到 ASTPool。 */
void TypeChecker::popScope(std::optional<uint32_t> block_exit_key, std::optional<uint32_t> func_exit_key) {
    if (!block_exit_key.has_value() && !func_exit_key.has_value()) {
        while (!symbols.empty() && symbols.back().depth == currentDepth) {
            symbols.pop_back();
        }
        currentDepth--;
        return;
    }

    std::vector<uint32_t> names;
    names.reserve(8);
    for (int i = static_cast<int>(symbols.size()) - 1; i >= 0; --i) {
        if (symbols[i].depth < currentDepth) {
            break;
        }
        if (symbols[i].is_owned && !symbols[i].is_moved) {
            names.push_back(symbols[i].name_id);
        }
    }
    if (block_exit_key.has_value()) {
        currentPool->block_exit_free_name_ids[*block_exit_key] = std::move(names);
    } else if (func_exit_key.has_value()) {
        currentPool->func_exit_free_name_ids[*func_exit_key] = std::move(names);
    }

    while (!symbols.empty() && symbols.back().depth == currentDepth) {
        symbols.pop_back();
    }
    currentDepth--;
}

void TypeChecker::endBlockScope(syntax::ASTNodeIndex block_stmt_node) {
    popScope(block_stmt_node.index, std::nullopt);
}

void TypeChecker::endFunctionLocalScope(syntax::ASTNodeIndex function_decl_node) {
    popScope(std::nullopt, function_decl_node.index);
}

void TypeChecker::endScopePlain() { popScope(std::nullopt, std::nullopt); }

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
            reportError(line, column, "Variable already declared in this scope.",
                        niki::diagnostic::events::SemanticCode::DuplicateDeclaration);
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
            if (symbols[i].is_moved) {
                reportError(line, column, "Use of moved value.",
                            niki::diagnostic::events::SemanticCode::UseOfMovedValue);
                return NKType::makeUnknown();
            }
            return symbols[i].type;
        }
    }

    // 通过 ModuleNamespace 查询同模块符号（O(1) per-module）
    if (moduleNamespace != nullptr && currentModuleId != kInvalidModuleId) {
        if (const auto *ns_sym = moduleNamespace->find(currentModuleId, name_id); ns_sym != nullptr) {
            return ns_sym->type;
        }
    }
    if (visibleSymbols != nullptr) {
        auto it = visibleSymbols->tables.find(name_id);
        if (it != visibleSymbols->tables.end()) {
            return it->second.type;
        }
    }

    reportError(line, column, "Undeclared variable.",
                niki::diagnostic::events::SemanticCode::UndeclaredIdentifier);
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

        // 1) 通过 ModuleNamespace 查询同模块符号
        if (moduleNamespace != nullptr && currentModuleId != kInvalidModuleId) {
            if (const auto *ns_sym = moduleNamespace->find(currentModuleId, name_id); ns_sym != nullptr) {
                if (ns_sym->kind == niki::Kind::Struct || ns_sym->kind == niki::Kind::Function ||
                    ns_sym->kind == niki::Kind::TypeAlias) {
                    return ns_sym->type;
                }
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
void TypeChecker::reportError(uint32_t line, uint32_t column, const std::string &message,
                               niki::diagnostic::events::SemanticCode code) {
    diagnostics.error(code, message,
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