#include "niki/l0_core/semantic/global_symbol_table.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/semantic/type_checker.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include <cstdint>

namespace niki::semantic {

// 两遍扫描第一遍：仅把「已在 GlobalSymbolTable 中的顶层符号」绑定进栈，类型句柄与 Driver 预声明一致
//（GlobalTypeArena 的 struct / func sig id）。调用方须先 predeclare，否则报错而非回落池内下标。

/**
 * @brief 预声明分发入口：仅处理可前向引用的顶层声明。
 * @param declIdx 声明节点索引。
 */
/**
 * @brief 预声明分发入口：仅处理可前向引用的顶层声明。
 * @param declIdx 声明节点索引。
 */
void TypeChecker::preDeclareNode(syntax::ASTNodeIndex declIdx) {
    if (!declIdx.isvalid())
        return;
    const auto &node = currentPool->getNode(declIdx);
    if (node.type == syntax::NodeType::FunctionDecl) {
        preDeclareFunction(declIdx);
    } else if (node.type == syntax::NodeType::StructDecl) {
        preDeclareStruct(declIdx);
    } else if (node.type == syntax::NodeType::TypeAliasDecl) {
        preDeclareTypeAlias(declIdx);
    } else if (node.type == syntax::NodeType::ComponentDecl) {
        preDeclareComponent(declIdx);
    }
}

/**
 * @brief 预声明结构体符号到当前语义作用域。
 * @param nodeIdx StructDecl 节点索引。
 */
/**
 * @brief 预声明结构体符号到当前语义作用域。
 * @param nodeIdx StructDecl 节点索引。
 */
void TypeChecker::preDeclareStruct(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    uint32_t struct_idx = node.payload.struct_decl.struct_index;
    const syntax::StructData &struct_data = currentPool->struct_data[struct_idx];

    const niki::GlobalSymbol *sym = globalSymbols->find(struct_data.name_id);
    if (sym == nullptr || sym->kind != niki::Kind::Struct) {
        reportError(line, column,
                    "Top-level struct missing from global symbol table; ensure predeclare ran before typecheck.");
        return;
    }
    declareSymbol(struct_data.name_id, sym->type, line, column);
}

/**
 * @brief 预声明函数符号到当前语义作用域。
 * @param nodeIdx FunctionDecl 节点索引。
 */
/**
 * @brief 预声明函数符号到当前语义作用域。
 * @param nodeIdx FunctionDecl 节点索引。
 */
void TypeChecker::preDeclareFunction(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    const syntax::FunctionData &func_data = currentPool->function_data[node.payload.func_decl.function_index];

    const niki::GlobalSymbol *sym = globalSymbols->find(func_data.name_id);
    if (sym == nullptr || sym->kind != niki::Kind::Function) {
        reportError(line, column,
                    "Top-level function missing from global symbol table; ensure predeclare ran before typecheck.");
        return;
    }
    declareSymbol(func_data.name_id, sym->type, line, column);
}

/**
 * @brief 预声明类型别名符号到当前语义作用域。
 * @param nodeIdx TypeAliasDecl 节点索引。
 */
void TypeChecker::preDeclareTypeAlias(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    const uint32_t alias_name_id = node.payload.type_alias.name_id;

    const niki::GlobalSymbol *sym = globalSymbols->find(alias_name_id);
    if (sym == nullptr || sym->kind != niki::Kind::TypeAlias) {
        reportError(line, column,
                    "Top-level type alias missing from global symbol table; ensure predeclare ran before typecheck.");
        return;
    }
    declareSymbol(alias_name_id, sym->type, line, column);
}

/**
 * @brief 预声明组件符号到 moduleComponentNames（不进入 symbol 栈）。
 * @param nodeIdx ComponentDecl 节点索引。
 * @note Component 不是值类型，不调用 declareSymbol。
 * 只注册到 moduleComponentNames 供 kits 窗口目标合法性校验使用。
 */
void TypeChecker::preDeclareComponent(syntax::ASTNodeIndex nodeIdx) {
    if (!nodeIdx.isvalid()) {
        return;
    }
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    const uint32_t name_id = node.payload.component_decl.name_id;
    if (name_id != 0) {
        moduleComponentNames.insert(name_id);
    }
}

} // namespace niki::semantic
