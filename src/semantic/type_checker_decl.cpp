#include "niki/semantic/type_checker.hpp"

namespace niki::semantic {

void TypeChecker::checkDeclaration(syntax::ASTNodeIndex declIdx) {
    const auto &node = currentPool->getNode(declIdx);
    switch (node.type) {
    case syntax::NodeType::FunctionDecl:
        checkFunctionDecl(declIdx);
        break;
    case syntax::NodeType::InterfaceMethod:
        checkInterfaceMethod(declIdx);
        break;
    case syntax::NodeType::StructDecl:
        checkStructDecl(declIdx);
        break;
    case syntax::NodeType::EnumDecl:
        checkEnumDecl(declIdx);
        break;
    case syntax::NodeType::TypeAliasDecl:
        checkTypeAliasDecl(declIdx);
        break;
    case syntax::NodeType::InterfaceDecl:
        checkInterfaceDecl(declIdx);
        break;
    case syntax::NodeType::ImplDecl:
        checkImplDecl(declIdx);
        break;
    case syntax::NodeType::ModuleDecl:
        checkModuleDecl(declIdx);
        break;
    case syntax::NodeType::SystemDecl:
        checkSystemDecl(declIdx);
        break;
    case syntax::NodeType::ComponentDecl:
        checkComponentDecl(declIdx);
        break;
    case syntax::NodeType::FlowDecl:
        checkFlowDecl(declIdx);
        break;
    case syntax::NodeType::KitsDecl:
        checkKitsDecl(declIdx);
        break;
    case syntax::NodeType::TagDecl:
        checkTagDecl(declIdx);
        break;
    case syntax::NodeType::TagGroupDecl:
        checkTagGroupDecl(declIdx);
        break;
    case syntax::NodeType::ProgramRoot:
        checkProgramRoot(declIdx);
        break;
    default:
        break;
    }
}

void TypeChecker::checkModuleDecl(syntax::ASTNodeIndex nodeIdx) {
    const auto &node = currentPool->getNode(nodeIdx);
    const auto &bodyNode = currentPool->getNode(node.payload.module_decl.body);
    auto declarations = currentPool->get_list(bodyNode.payload.list.elements);

    for (auto child : declarations) {
        checkNode(child);
    }
}

void TypeChecker::checkProgramRoot(syntax::ASTNodeIndex nodeIdx) { checkModuleDecl(nodeIdx); }
void TypeChecker::checkFunctionDecl(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkInterfaceMethod(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkStructDecl(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkEnumDecl(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkTypeAliasDecl(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkInterfaceDecl(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkImplDecl(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkSystemDecl(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkComponentDecl(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkFlowDecl(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkKitsDecl(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkTagDecl(syntax::ASTNodeIndex nodeIdx) {}
void TypeChecker::checkTagGroupDecl(syntax::ASTNodeIndex nodeIdx) {}

} // namespace niki::semantic