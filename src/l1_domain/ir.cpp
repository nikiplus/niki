#include "niki/l1_domain/ir.hpp"
#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include <unordered_set>

namespace niki::ir {
using namespace niki::syntax;

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

    if (!decl.payload.kits_decl.body.isvalid()) {
        error(bc, "kits declaration missing body.", decl_idx);
        return false;
    }

    const ASTNode &body = bc.unit->pool.getNode(decl.payload.kits_decl.body);
    const auto members = bc.unit->pool.get_list(body.payload.list.elements);
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

    if (component_decl.is_struct_promotion) {
        record.source_struct_sid = bc.module.intern(bc.unit->pool.getStringId(component_decl.source_struct_name_id));
    }

    bc.module.components.push_back(record);
    return true;
}
} // namespace niki::ir

namespace niki::l1_domain {
void registerIRExtensions() {}
} // namespace niki::l1_domain

