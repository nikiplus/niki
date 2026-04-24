#include "niki/l0_core/semantic/global_symbol_table.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/semantic/type_checker.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include <cstdint>

namespace niki::semantic {

// 两遍扫描第一遍：仅把「已在 GlobalSymbolTable 中的顶层符号」绑定进栈，类型句柄与 Driver 预声明一致
//（GlobalTypeArena 的 struct / func sig id）。调用方须先 predeclare，否则报错而非回落池内下标。

void TypeChecker::preDeclareNode(syntax::ASTNodeIndex declIdx) {
    if (!declIdx.isvalid())
        return;
    const auto &node = currentPool->getNode(declIdx);
    if (node.type == syntax::NodeType::FunctionDecl) {
        preDeclareFunction(declIdx);
    } else if (node.type == syntax::NodeType::StructDecl) {
        preDeclareStruct(declIdx);
    }
}

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

} // namespace niki::semantic
