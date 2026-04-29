#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/syntax/ast.hpp"

/** @builder_decl_impl: 顶层与声明降解实现
 * 这个文件处理模块根与顶层声明的降解入口，核心目标是把“树形声明结构”转成“函数级 IR 组织边界”。
 * 它定义了函数如何被注册、入口块如何建立、形参如何绑定到初始寄存器窗口。
 *
 * 从流水线角度看，这里是 builder 的第一道结构化关口：
 * 根节点非法会在此直接失败，函数声明会在此完成最小可执行骨架（func + entry block）搭建，
 * 之后才会进入语句/表达式子流程补齐函数体指令。
 *
 * 这使得 IR 模块组织（函数边界、块边界）在早期就被固定下来，避免后续阶段出现“指令已发射但归属不清”的状态。
 *
 * 字段流说明（核心）：
 * - `bc.unit->root` -> `buildRoot`：决定顶层遍历起点。
 * - `FunctionDecl` -> `FuncRecord`：函数声明先注册函数记录，再建立 entry block。
 * - `params` -> `fc.local_vreg_by_name_sid`：形参名先绑定寄存器，函数体才能按名解析。
 * - `func_data.body` -> `buildStmt`：函数体降级在已有函数/块骨架上继续填充指令。
 */
namespace niki::ir {
using namespace niki::syntax;

/**
 * @brief 降级模块根节点并分发顶层声明。
 * @param bc 全局构建上下文（含 compilation unit 与 module 容器）。
 * @return true 根节点与顶层声明分发成功。
 * @return false 根节点非法或子声明降级失败；错误写入 bc.diags。
 * @note 失败场景包括 root 无效、root 类型非法、子声明处理失败。
 */
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

/**
 * @brief 分发顶层声明到对应降级路径。
 * @param bc 全局构建上下文。
 * @param decl_idx 顶层声明节点索引。
 * @return true 声明成功处理，或属于当前阶段可忽略类型。
 * @return false 下游声明降级失败。
 */
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

/**
 * @brief 将函数声明降级为 IR 函数记录与函数体指令。
 * @param bc 全局构建上下文。
 * @param decl_idx 函数声明节点索引。
 * @return true 成功建立 FuncRecord/entry block 并完成函数体降级。
 * @return false 函数体降级失败或块终结保障失败。
 * @note 形参会先绑定到本函数局部寄存器表。
 */
bool IRBuilder::buildFuncDecl(BuildCtx &bc, ASTNodeIndex decl_idx) {
    const ASTNode &decl = bc.unit->pool.getNode(decl_idx);
    const auto &func_data = bc.unit->pool.function_data[decl.payload.func_decl.function_index];

    FuncCtx fc;
    fc.fid = beginFunc(bc, func_data.name_id);

    const BlockId entry = beginBlock(bc, fc, "entry");
    func(bc, fc.fid).entry_block = entry;
    switchBlock(fc, entry);
    setEmitLocation(bc, fc, decl_idx);

    // PARAM_BIND: 形参名绑定到本函数局部寄存器表。
    auto param_nodes = bc.unit->pool.get_list(func_data.params);
    func(bc, fc.fid).param_count = static_cast<uint32_t>(param_nodes.size());
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
