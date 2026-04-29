#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/syntax/ast.hpp"

namespace niki::ir {
using namespace niki::syntax;

//------------------------------------------------------------------------------
// ROOT: 顶层节点降解，负责模块根与声明列表分发。
//------------------------------------------------------------------------------
bool IRBuilder::buildRoot(BuildCtx &bc) {
    if (bc.unit == nullptr || !bc.unit->root.isvalid()) {
        bc.diags.error(diagnostic::events::IRCode::InvalidRoot, "invalid module root",
                       diagnostic::makeSourceSpan(bc.unit ? bc.unit->source_path : ""));
        return false;
    }
    const ASTNode &root = bc.unit->pool.getNode(bc.unit->root);
    if (root.type != NodeType::ModuleDecl && root.type != NodeType::ProgramRoot) {
        bc.diags.error(diagnostic::events::IRCode::InvalidRoot,
                       "IR builder expects ModuleDecl or ProgramRoot root.",
                       diagnostic::makeSourceSpan(bc.unit->source_path));
        return false;
    }
    ASTNodeIndex body_idx = (root.type == NodeType::ModuleDecl) ? root.payload.module_decl.body : bc.unit->root;
    const ASTNode &body = bc.unit->pool.getNode(body_idx);
    auto decls = bc.unit->pool.get_list(body.payload.list.elements);
    bool ok = true;
    for (ASTNodeIndex decl_index : decls) {
        ok = buildTopDecl(bc, decl_index) && ok;
    }
    return ok;
}

//------------------------------------------------------------------------------
// DECL_ENTRY: 顶层声明降解入口。
//------------------------------------------------------------------------------
bool IRBuilder::buildTopDecl(BuildCtx &bc, ASTNodeIndex decl_idx) {
    if (!decl_idx.isvalid())
        return true;
    const ASTNode &decl = bc.unit->pool.getNode(decl_idx);
    if (decl.type == NodeType::FunctionDecl)
        return buildFuncDecl(bc, decl_idx);
    if (decl.type == NodeType::StructDecl)
        return true;
    return true;
}

//------------------------------------------------------------------------------
// FUNC_DECL: 函数声明降解，完成参数寄存器绑定与函数体发射。
//------------------------------------------------------------------------------
bool IRBuilder::buildFuncDecl(BuildCtx &bc, ASTNodeIndex decl_idx) {
    const ASTNode &decl = bc.unit->pool.getNode(decl_idx);
    const auto &func_data = bc.unit->pool.function_data[decl.payload.func_decl.function_index];
    FuncCtx fc;
    fc.fid = beginFunc(bc, func_data.name_id);
    const BlockId entry = beginBlock(bc, fc, "entry");
    func(bc, fc.fid).entry_block = entry;
    switchBlock(fc, entry);
    setEmitLocation(bc, fc, decl_idx);

    auto param_nodes = bc.unit->pool.get_list(func_data.params);
    for (ASTNodeIndex param_idx : param_nodes) {
        if (!param_idx.isvalid()) {
            continue;
        }
        const ASTNode &param_node = bc.unit->pool.getNode(param_idx);
        const uint32_t name_sid = param_node.payload.var_decl.name_id;
        const RegId reg = allocVReg(bc, fc);
        fc.local_vreg_by_name_sid[name_sid] = reg;
    }

    bool ok = buildStmt(bc, fc, func_data.body);
    ok = ensureBlockTerminated(bc, fc) && ok;
    return ok;
}

} // namespace niki::ir
