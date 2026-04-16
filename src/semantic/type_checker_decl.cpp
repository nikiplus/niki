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

    // 第一遍扫描：预声明所有顶层符号 (Two-Pass Compilation 第一步)
    for (auto child : declarations) {
        preDeclareNode(child);
    }

    // 第二遍扫描：检查具体的函数体和声明细节
    for (auto child : declarations) {
        checkNode(child);
    }
}

void TypeChecker::checkProgramRoot(syntax::ASTNodeIndex nodeIdx) { checkModuleDecl(nodeIdx); }
void TypeChecker::checkFunctionDecl(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    const syntax::FunctionData &func_data = currentPool->function_data[node.payload.func_decl.function_index];

    // 提取签名 (由于我们在 preDeclareFunction 已经做过一次，这里为了获取 paramTypes 我们再提取一次，
    // 也可以将结果缓存在某个地方，但对于 MVP 来说重新解析一遍开销极小)
    std::span<const syntax::ASTNodeIndex> paramNodes = currentPool->get_list(func_data.params);
    std::vector<NKType> paramTypes;
    for (size_t i = 0; i < paramNodes.size(); ++i) {
        const syntax::ASTNode &paramNode = currentPool->getNode(paramNodes[i]);
        syntax::ASTNodeIndex type_expr_idx = paramNode.payload.var_decl.type_expr;
        paramTypes.push_back(resolveTypeAnnotation(type_expr_idx));
    }

    NKType retType = NKType(NKBaseType::Void, -1);
    if (func_data.return_type.isvalid()) {
        retType = resolveTypeAnnotation(func_data.return_type);
    }

    // 进入函数作用域
    beginScope();

    // 注册参数到局部作用域
    for (size_t i = 0; i < paramNodes.size(); ++i) {
        auto [paramNode, p_line, p_col] = getNodeCtx(paramNodes[i]);
        uint32_t param_name_id = paramNode.payload.var_decl.name_id;
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
void TypeChecker::checkStructDecl(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    uint32_t struct_idx = node.payload.struct_decl.struct_index;
    const syntax::StructData &struct_data = currentPool->struct_data[struct_idx];

    std::span<const syntax::ASTNodeIndex> types = currentPool->get_list(struct_data.types);
    for (size_t i = 0; i < types.size(); ++i) {
        resolveTypeAnnotation(types[i]); // 这里会隐式地报错如果类型不存在
    }
}
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