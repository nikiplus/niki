#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"

namespace niki::syntax {

//---顶层声明编译---
void Compiler::compileDeclaration(ASTNodeIndex nodeIdx) {
    if (!nodeIdx.isvalid()) {
        return;
    }
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    switch (node.type) {
    // case NodeType::FunctionDecl:
    //     ...
    default:
        // 如果遇到无法识别的声明，可能退化为普通语句
        compileStatement(nodeIdx);
        break;
    }
}

void Compiler::compileKitsDecl(ASTNodeIndex nodeIdx) {}

// todo: 其他声明类型编译

} // namespace niki::syntax