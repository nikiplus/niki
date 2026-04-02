#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"
#include <span>

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
    case NodeType::ModuleDecl: {
        std::span<const ASTNodeIndex> exports = currentPool->get_list(node.payload.module_decl.exports);
        for (size_t i = 0; i < exports.size(); ++i) {
            ASTNodeIndex child = exports[i];
            const ASTNode &childNode = currentPool->getNode(child);
            
            // 针对 REPL 最小回路：如果我们遇到的是最后一条表达式语句（ExpressionStmt）
            if (childNode.type == NodeType::ExpressionStmt && i == exports.size() - 1) {
                // 取出里面真正的表达式，并强制编译到寄存器 0
                compileExpressionWithTarget(childNode.payload.expr_stmt.expression, 0);
            } else if (childNode.type >= NodeType::BinaryExpr && childNode.type <= NodeType::ImplicitCastExpr) {
                // 向后兼容：如果由于某种原因 Parser 丢出了裸表达式
                if (i == exports.size() - 1) {
                    compileExpressionWithTarget(child, 0);
                } else {
                    ExprResult res = compileExpression(child);
                    freeIfTemp(res);
                }
            } else {
                compileNode(child);
            }
        }
        break;
    }
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