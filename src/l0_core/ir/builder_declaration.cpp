#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include <unordered_set>

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

    // 若存在显式 module 声明，IR 模块名以语义模块名为准（而非 source_path）。
    if (root.type == NodeType::ModuleDecl &&
        root.payload.module_decl.name_id < static_cast<uint32_t>(bc.module.string_pool.size())) {
        bc.module.module_name = bc.unit->pool.getStringId(root.payload.module_decl.name_id);
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
    auto mark_kits_exported_by_name_sid = [&](uint32_t kits_name_sid) {
        bc.exported_kits_name_sids.insert(kits_name_sid);
        for (auto &kits_record : bc.module.kits) {
            if (kits_record.kits_sid == kits_name_sid) {
                kits_record.is_exported = true;
            }
        }
    };
    auto mark_component_exported_by_name_sid = [&](uint32_t component_name_sid) {
        bc.exported_component_name_sids.insert(component_name_sid);
        for (auto &component_record : bc.module.components) {
            if (component_record.component_sid == component_name_sid) {
                component_record.is_exported = true;
            }
        }
    };

    if (decl.type == NodeType::ModuleDecl) {
        if (decl.payload.module_decl.name_id < static_cast<uint32_t>(bc.module.string_pool.size())) {
            bc.module.module_name = bc.unit->pool.getStringId(decl.payload.module_decl.name_id);
        }
        if (!decl.payload.module_decl.body.isvalid()) {
            error(bc, "Module declaration missing body in IR lowering.", decl_idx);
            return false;
        }
        const ASTNode &module_body = bc.unit->pool.getNode(decl.payload.module_decl.body);
        auto nested_decls = bc.unit->pool.get_list(module_body.payload.list.elements);
        bool ok = true;
        for (ASTNodeIndex nested_decl_idx : nested_decls) {
            ok = buildTopDecl(bc, nested_decl_idx) && ok;
        }
        return ok;
    }

    if (decl.type == NodeType::ExportDecl) {
        const auto &export_decl_data = bc.unit->pool.export_decl_data[decl.payload.export_decl.export_decl_index];

        if (export_decl_data.has_wrapped_decl && export_decl_data.wrapped_decl.isvalid()) {
            const ASTNode &wrapped_decl = bc.unit->pool.getNode(export_decl_data.wrapped_decl);
            bool ok = buildTopDecl(bc, export_decl_data.wrapped_decl);
            if (wrapped_decl.type == NodeType::KitsDecl) {
                const uint32_t kits_name_sid =
                    bc.module.intern(bc.unit->pool.getStringId(wrapped_decl.payload.kits_decl.name_id));
                mark_kits_exported_by_name_sid(kits_name_sid);
            } else if (wrapped_decl.type == NodeType::ComponentDecl) {
                const uint32_t component_name_sid =
                    bc.module.intern(bc.unit->pool.getStringId(wrapped_decl.payload.component_decl.name_id));
                mark_component_exported_by_name_sid(component_name_sid);
            }
            return ok;
        }

        for (uint32_t i = 0; i < export_decl_data.item_count; ++i) {
            const ExportItem &item = bc.unit->pool.export_items[export_decl_data.first_item_index + i];
            const uint32_t exported_name_sid = bc.module.intern(bc.unit->pool.getStringId(item.local_name_id));
            mark_kits_exported_by_name_sid(exported_name_sid);
            mark_component_exported_by_name_sid(exported_name_sid);
        }
        return true;
    }

    if (decl.type == NodeType::FunctionDecl)
        return buildFuncDecl(bc, decl_idx);

    if (decl.type == NodeType::StructDecl)
        return true;

    if (decl.type == NodeType::ComponentDecl)
        return buildComponentDecl(bc, decl_idx);

    if (decl.type == NodeType::KitsDecl)
        return buildKitsDecl(bc, decl_idx);

    return true;
}

bool IRBuilder::buildKitsDecl(BuildCtx &bc, ASTNodeIndex decl_idx) {
    if (!decl_idx.isvalid()) {
        return true;
    }

    const ASTNode &decl = bc.unit->pool.getNode(decl_idx);
    const uint32_t kits_sid = bc.module.intern(bc.unit->pool.getStringId(decl.payload.kits_decl.name_id));
    const uint32_t owner_mod_sid = bc.module.intern(bc.module.module_name);

    KitsRecord kits_record{};
    kits_record.kits_sid = kits_sid;
    kits_record.owner_mod_sid = owner_mod_sid;
    kits_record.first_item = static_cast<uint32_t>(bc.module.kits_items.size());
    kits_record.is_exported = (bc.exported_kits_name_sids.find(kits_sid) != bc.exported_kits_name_sids.end());

    if (!decl.payload.kits_decl.body.isvalid()) {
        error(bc, "kits declaration missing body.", decl_idx);
        return false;
    }

    const ASTNode &body = bc.unit->pool.getNode(decl.payload.kits_decl.body);
    auto members = bc.unit->pool.get_list(body.payload.list.elements);
    std::unordered_set<uint32_t> seen_alias_name_ids;
    seen_alias_name_ids.reserve(members.size());
    for (ASTNodeIndex member_idx : members) {
        if (!member_idx.isvalid()) {
            error(bc, "Invalid kits member node.", decl_idx);
            return false;
        }

        const ASTNode &member = bc.unit->pool.getNode(member_idx);
        if (member.type != NodeType::VarDeclStmt && member.type != NodeType::ConstDeclStmt) {
            error(bc, "Invalid kits member declaration in IR lowering.", member_idx);
            return false;
        }

        const ASTNodeIndex type_expr_idx = member.payload.var_decl.type_expr;
        if (!type_expr_idx.isvalid()) {
            error(bc, "kits member missing component type expression in IR lowering.", member_idx);
            return false;
        }
        const ASTNode &type_expr = bc.unit->pool.getNode(type_expr_idx);
        if (type_expr.type != NodeType::IdentifierExpr) {
            error(bc, "kits component type must be identifier in IR lowering.", member_idx);
            return false;
        }

        const uint32_t alias_name_id = member.payload.var_decl.name_id;
        if (!seen_alias_name_ids.insert(alias_name_id).second) {
            error(bc, "Duplicate kits alias in IR lowering.", member_idx);
            return false;
        }

        KitsItemRecord item{};
        item.alias_sid = bc.module.intern(bc.unit->pool.getStringId(alias_name_id));
        item.component_sid = bc.module.intern(bc.unit->pool.getStringId(type_expr.payload.identifier.name_id));
        item.is_mutable = (member.type == NodeType::VarDeclStmt);
        bc.module.kits_items.push_back(item);
        ++kits_record.item_count;
    }

    bc.module.kits.push_back(kits_record);
    return true;
}

bool IRBuilder::buildComponentDecl(BuildCtx &bc, ASTNodeIndex decl_idx) {
    if (!decl_idx.isvalid()) {
        return true;
    }
    const ASTNode &decl = bc.unit->pool.getNode(decl_idx);
    const auto &component_decl = decl.payload.component_decl;

    ComponentRecord record{};
    record.component_sid = bc.module.intern(bc.unit->pool.getStringId(component_decl.name_id));
    record.owner_mod_sid = bc.module.intern(bc.module.module_name);
    record.is_struct_promotion = component_decl.is_struct_promotion;
    record.is_exported =
        (bc.exported_component_name_sids.find(record.component_sid) != bc.exported_component_name_sids.end());

    if (component_decl.is_struct_promotion) {
        record.source_struct_sid = bc.module.intern(bc.unit->pool.getStringId(component_decl.source_struct_name_id));
    }

    bc.module.components.push_back(record);
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
    uint32_t param_count = 0;
    for (ASTNodeIndex param_idx : param_nodes) {
        if (!param_idx.isvalid()) {
            continue;
        }
        ++param_count;
        const ASTNode &param_node = bc.unit->pool.getNode(param_idx);
        const uint32_t name_sid = param_node.payload.var_decl.name_id;
        const RegId reg = allocVReg(bc, fc);
        fc.local_vreg_by_name_sid[name_sid] = reg;
    }
    func(bc, fc.fid).arity = param_count;

    bool ok = buildStmt(bc, fc, func_data.body);
    ok = ensureBlockTerminated(bc, fc) && ok;

    return ok;
}

} // namespace niki::ir
