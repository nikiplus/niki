#include "niki/l0_core/ir/builder.hpp"

#include "niki/l0_core/syntax/ast.hpp"

namespace niki::ir {
using namespace niki::syntax;

bool IRBuilder::buildTopLvDecl(ASTNodeIndex decl_idx) {
    if (!decl_idx.isvalid()) {
        return true;
    }
    const ASTNode &decl = unit_->pool.getNode(decl_idx);

    switch (decl.type) {
    case NodeType::FunctionDecl:
        return buildFuncDecl(decl_idx);
    case NodeType::StructDecl:
        return buildStructDecl(decl_idx);
    case NodeType::InterfaceMethod:
    case NodeType::EnumDecl:
    case NodeType::TypeAliasDecl:
    case NodeType::InterfaceDecl:
    case NodeType::ImplDecl:
    case NodeType::ImportDecl:
    case NodeType::ExportDecl:
    case NodeType::SystemDecl:
    case NodeType::ComponentDecl:
    case NodeType::FlowDecl:
    case NodeType::KitsDecl:
    case NodeType::TagDecl:
    case NodeType::TagGroupDecl:
        reportError("Top-level declaration is recognized but not implemented in IRBuilder yet.", decl_idx);
        return false;
    default:
        reportError("Top-level declaration node is not supported by IRBuilder.", decl_idx);
        return false;
    }
}

bool IRBuilder::buildStructDecl(ASTNodeIndex decl_idx) {
    const ASTNode &decl = unit_->pool.getNode(decl_idx);
    const auto &struct_data = unit_->pool.struct_data[decl.payload.struct_decl.struct_index];
    (void)ensureSymbol(struct_data.name_id, IRSymbolKind::Struct, IRType::makeUnknown(), true);
    return true;
}

bool IRBuilder::buildFuncDecl(ASTNodeIndex decl_idx) {
    const ASTNode &decl = unit_->pool.getNode(decl_idx);
    const auto &func_data = unit_->pool.function_data[decl.payload.func_decl.function_index];

    IRFunction &func = module_ir_.createFunc(func_data.name_id, unit_->source_path);
    func.func_sig.return_type = IRType::makeUnknown();

    FuncBuildCtx ctx;
    ctx.func = &func;

    IRBasicBlock &entry = appendBlock(ctx, "entry");
    func.entry_block_id = entry.block_id;
    switchToBlock(ctx, entry);

    auto param_nodes = unit_->pool.get_list(func_data.params);
    for (ASTNodeIndex param_idx : param_nodes) {
        const ASTNode &param_node = unit_->pool.getNode(param_idx);
        uint32_t param_name_id = param_node.payload.var_decl.name_id;
        IRRegId reg_id = func.allocateVirtualRegister();
        func.parameter_registers.push_back(reg_id);
        ctx.local_reg_by_name_id[param_name_id] = reg_id;
    }

    bool ok = buildStmt(ctx, func_data.body);
    ok = ensureBlockTerminated(ctx) && ok;

    IRSymbolId sym_id = ensureSymbol(func_data.name_id, IRSymbolKind::Function, IRType::makeUnknown(), true);
    if (sym_id < module_ir_.sym_table.size()) {
        module_ir_.sym_table[sym_id].owner_func_id = func.func_id;
    }
    return ok;
}

} // namespace niki::ir
