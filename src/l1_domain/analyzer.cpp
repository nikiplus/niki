#include "niki/l1_domain/analyzer.hpp"
#include "niki/l0_core/semantic/extensions.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/semantic/type_checker.hpp"
#include "niki/l0_core/syntax/ast.hpp"

namespace niki::semantic {

void TypeChecker::checkSystemDecl(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    bool enclosing_system = inSystemContext;
    inSystemContext = true;
    if (node.payload.system_decl.body.isvalid()) {
        checkStatement(node.payload.system_decl.body);
    }
    inSystemContext = enclosing_system;
}

void TypeChecker::checkComponentDecl(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    const auto &component_decl = node.payload.component_decl;

    if (component_decl.is_struct_promotion) {
        if (component_decl.body.isvalid()) {
            reportError(line, column, "Promoted component must not have body.");
            return;
        }
        if (component_decl.source_struct_name_id == 0) {
            reportError(line, column, "Promoted component missing source struct name.");
            return;
        }

        bool struct_found = false;
        if (moduleNamespace != nullptr && currentModuleId != kInvalidModuleId) {
            const auto *sym = moduleNamespace->find(currentModuleId, component_decl.source_struct_name_id);
            struct_found = (sym != nullptr && sym->kind == niki::Kind::Struct);
        }
        if (!struct_found && visibleSymbols != nullptr) {
            auto iter = visibleSymbols->tables.find(component_decl.source_struct_name_id);
            if (iter != visibleSymbols->tables.end() && iter->second.kind == niki::Kind::Struct) {
                struct_found = true;
            }
        }
        if (!struct_found) {
            reportError(line, column, "Promoted component source struct not found.");
        }
        return;
    }

    if (!component_decl.body.isvalid()) {
        reportError(line, column, "Component declaration missing body.");
        return;
    }

    // 递归检查 component body 内部的成员声明
    beginScope();
    checkStatement(component_decl.body);
    endScopePlain();
}

void TypeChecker::checkFlowDecl(syntax::ASTNodeIndex nodeIdx) {}

void TypeChecker::checkKitsDecl(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    if (!node.payload.kits_decl.body.isvalid()) {
        reportError(line, column, "Invalid kits body.");
        return;
    }

    const auto &body_node = currentPool->getNode(node.payload.kits_decl.body);
    auto members = currentPool->get_list(body_node.payload.list.elements);
    auto &window = kitsWindows[node.payload.kits_decl.name_id];
    window.clear();

    for (auto member_idx : members) {
        if (!member_idx.isvalid()) {
            continue;
        }
        const auto [member_node, m_line, m_col] = getNodeCtx(member_idx);
        const bool is_mutable = member_node.type == syntax::NodeType::VarDeclStmt;
        if (member_node.type != syntax::NodeType::VarDeclStmt && member_node.type != syntax::NodeType::ConstDeclStmt) {
            reportError(m_line, m_col, "Invalid kits member declaration.");
            continue;
        }

        const uint32_t alias_name_id = member_node.payload.var_decl.name_id;
        const auto type_expr_idx = member_node.payload.var_decl.type_expr;
        if (member_node.payload.var_decl.init_expr.isvalid()) {
            reportError(m_line, m_col, "Kits member must not have initializer.");
            continue;
        }
        if (!type_expr_idx.isvalid()) {
            reportError(m_line, m_col, "Kits member missing component type expression.");
            continue;
        }
        const auto &type_expr_node = currentPool->getNode(type_expr_idx);
        if (type_expr_node.type != syntax::NodeType::IdentifierExpr) {
            reportError(m_line, m_col, "Kits component name must be an identifier.");
            continue;
        }
        const uint32_t component_name_id = type_expr_node.payload.identifier.name_id;
        if (moduleComponentNames.find(component_name_id) == moduleComponentNames.end()) {
            reportError(m_line, m_col, "Kits target must be a component declared in current module.");
            continue;
        }

        if (window.find(alias_name_id) != window.end()) {
            reportError(m_line, m_col, "Duplicate kits alias in same kits scope.");
            continue;
        }
        window.emplace(alias_name_id, TypeChecker::KitsWindowEntry{
                                         .component_name_id = component_name_id,
                                         .is_mutable = is_mutable,
                                     });
    }
}

void TypeChecker::checkTagDecl(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkTagGroupDecl(syntax::ASTNodeIndex nodeIdx) {}

} // namespace niki::semantic

namespace niki::l1_domain {
namespace {
bool handleDomainDecl(semantic::TypeChecker &checker, syntax::ASTNodeIndex decl_idx) {
    return false;
}
} // namespace

void registerSemanticExtensions() { semantic::registerDomainSemanticDeclHandler(handleDomainDecl); }
} // namespace niki::l1_domain

