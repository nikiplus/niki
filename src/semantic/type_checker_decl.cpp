#include "niki/semantic/type_checker.hpp"

namespace niki::semantic {

void TypeChecker::checkDeclaration(syntax::ASTNodeIndex declIdx) {
    const auto &node = currentPool->getNode(declIdx);
    if (node.type == syntax::NodeType::ModuleDecl || node.type == syntax::NodeType::ProgramRoot) {
        checkModuleDecl(declIdx);
    }
    // MVP 先跳过函数等声明
}

void TypeChecker::checkModuleDecl(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = currentPool->getNode(nodeIdx);
    const auto &bodyNode = currentPool->getNode(node.payload.module_decl.body);
    auto declarations = currentPool->get_list(bodyNode.payload.list.elements);

    for (auto child : declarations) {
        checkNode(child);
    }
}

} // namespace niki::semantic