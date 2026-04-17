#include "niki/semantic/nktype.hpp"
#include "niki/semantic/type_checker.hpp"
#include "niki/syntax/ast.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace niki::semantic {

// --- 顶层声明预声明 (两遍扫描的第一遍) ---
// 目标：先注册顶层可见符号，使第二遍检查支持前向引用。

void TypeChecker::preDeclareNode(syntax::ASTNodeIndex declIdx) {
    if (!declIdx.isvalid())
        return;
    const auto &node = currentPool->getNode(declIdx);
    if (node.type == syntax::NodeType::FunctionDecl) {
        preDeclareFunction(declIdx);
    } else if (node.type == syntax::NodeType::StructDecl) {
        preDeclareStruct(declIdx);
    }
    // 未来在这里增加对 InterfaceDecl 等全局类型的预声明
}

void TypeChecker::preDeclareStruct(syntax::ASTNodeIndex nodeIdx) {
    // 预声明结构体：将 `struct name` 注册为 Object(struct_idx) 符号。
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    uint32_t struct_idx = node.payload.struct_decl.struct_index;
    const syntax::StructData &struct_data = currentPool->struct_data[struct_idx];

    NKType structType(NKBaseType::Object, static_cast<int32_t>(struct_idx));
    declareSymbol(struct_data.name_id, structType, line, column);
}

void TypeChecker::preDeclareFunction(syntax::ASTNodeIndex nodeIdx) {
    // 预声明函数：提取签名并将函数名绑定为 Function(sig_id)。
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    const syntax::FunctionData &func_data = currentPool->function_data[node.payload.func_decl.function_index];

    // 提取签名
    std::span<const syntax::ASTNodeIndex> paramNodes = currentPool->get_list(func_data.params);
    std::vector<NKType> paramTypes;
    for (size_t i = 0; i < paramNodes.size(); ++i) {
        const syntax::ASTNode &paramNode = currentPool->getNode(paramNodes[i]);
        syntax::ASTNodeIndex type_expr_idx = paramNode.payload.var_decl.type_expr;
        paramTypes.push_back(resolveTypeAnnotation(type_expr_idx));
    }

    // 未标注返回类型时，先记录为 Unknown，避免把函数误判成固定 void 返回。
    NKType retType = NKType::makeUnknown();
    if (func_data.return_type.isvalid()) {
        retType = resolveTypeAnnotation(func_data.return_type);
    }

    // 将签名注册到签名池中，获取唯一的typeid
    uint32_t sig_id = const_cast<syntax::ASTPool *>(currentPool)->internFuncSignature(paramTypes, retType);
    NKType funcType(NKBaseType::Function, static_cast<int32_t>(sig_id));

    // 将函数类型注册到外层作用域
    declareSymbol(func_data.name_id, funcType, line, column);
}

} // namespace niki::semantic