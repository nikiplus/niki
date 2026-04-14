#include "niki/semantic/nktype.hpp"
#include "niki/semantic/type_checker.hpp"
#include "niki/syntax/ast.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

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
void TypeChecker::checkFunctionDecl(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    uint32_t func_index = node.payload.func_decl.function_index;
    const syntax::FunctionData &func_data = currentPool->function_data[node.payload.func_decl.function_index];

    // 提取签名
    std::span<const syntax::ASTNodeIndex> paramNodes = currentPool->get_list(func_data.params);
    std::vector<NKType> paramTypes;
    for (size_t i = 0; i < paramNodes.size(); ++i) {
        const syntax::ASTNode &paramNode = currentPool->getNode(paramNodes[i]);

        syntax::ASTNodeIndex type_expr_idx = paramNode.payload.var_decl.type_expr;

        paramTypes.push_back(resolveTypeAnnotation(type_expr_idx));
    }

    // 未来这里要解析func.data.returntype
    NKType retType = NKType::makeUnknown();

    // 将签名注册到签名池中，获取唯一的typeid
    uint32_t sig_id = const_cast<syntax::ASTPool *>(currentPool)->internFuncSignature(paramTypes, retType);
    NKType funcType(NKBaseType::Function, static_cast<int32_t>(sig_id));

    // 将函数类型注册到外层作用域
    declareSymbol(func_data.name_id, funcType, line, column);

    // 进入函数作用域
    beginScope();

    // 注册参数
    for (size_t i = 0; i < paramNodes.size(); ++i) {
        auto [paramNode, p_line, p_col] = getNodeCtx(paramNodes[i]);
        uint32_t param_name_id = paramNode.payload.identifier.name_id;

        // MVP阶段：我们尚未在语法中强制参数类型声明，所以参数类型暂时视为 Unknown
        declareSymbol(param_name_id, paramTypes[i], p_line, p_col);
    }

    NKType enclosingReturnType = currentReturnType;
    currentReturnType = retType;

    if (func_data.body.isvalid()) {
        checkStatement(func_data.body);
    }

    currentReturnType = enclosingReturnType;

    endScope();
}
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