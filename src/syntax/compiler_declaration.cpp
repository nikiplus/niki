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
    // uint32_t line = currentPool->locations[nodeIdx.index].line;
    // uint32_t column = currentPool->locations[nodeIdx.index].column;

    switch (node.type) {
    case NodeType::FunctionDecl:
        compileFunctionDecl(nodeIdx);
        break;
    case NodeType::InterfaceMethod:
        compileInterfaceMethod(nodeIdx);
        break;
    case NodeType::StructDecl:
        compileStructDecl(nodeIdx);
        break;
    case NodeType::EnumDecl:
        compileEnumDecl(nodeIdx);
        break;
    case NodeType::TypeAliasDecl:
        compileTypeAliasDecl(nodeIdx);
        break;
    case NodeType::InterfaceDecl:
        compileInterfaceDecl(nodeIdx);
        break;
    case NodeType::ImplDecl:
        compileImplDecl(nodeIdx);
        break;
    case NodeType::ModuleDecl:
        compileModuleDecl(nodeIdx);
        break;
    case NodeType::SystemDecl:
        compileSystemDecl(nodeIdx);
        break;
    case NodeType::ComponentDecl:
        compileComponentDecl(nodeIdx);
        break;
    case NodeType::FlowDecl:
        compileFlowDecl(nodeIdx);
        break;
    case NodeType::KitsDecl:
        compileKitsDecl(nodeIdx);
        break;
    case NodeType::TagDecl:
        compileTagDecl(nodeIdx);
        break;
    case NodeType::TagGroupDecl:
        compileTagGroupDecl(nodeIdx);
        break;
    case NodeType::ProgramRoot:
        compileProgramRoot(nodeIdx);
        break;
    case NodeType::ErrorNode:
        compileErrorNode(nodeIdx);
        break;
    default:
        // 如果遇到无法识别的声明，可能退化为普通语句
        compileStatement(nodeIdx);
        break;
    }
}

// 基础声明
void Compiler::compileFunctionDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileInterfaceMethod(ASTNodeIndex nodeIdx) {}
void Compiler::compileStructDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileEnumDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileTypeAliasDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileInterfaceDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileImplDecl(ASTNodeIndex nodeIdx) {}

// NIKI特有
void Compiler::compileModuleDecl(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    const ASTNode &bodyNode = currentPool->getNode(node.payload.module_decl.body);
    std::span<const ASTNodeIndex> declarations = currentPool->get_list(bodyNode.payload.list.elements);
    for (size_t i = 0; i < declarations.size(); ++i) {
        ASTNodeIndex child = declarations[i];
        const ASTNode &childNode = currentPool->getNode(child);

        // 针对 REPL 最小回路：如果我们遇到的是最后一条表达式语句（ExpressionStmt）
        if (childNode.type == NodeType::ExpressionStmt && i == declarations.size() - 1) {
            // 取出里面真正的表达式
            uint32_t line = currentPool->locations[child.index].line;
            uint32_t column = currentPool->locations[child.index].column;
            ExprResult exprRes = compileExpression(childNode.payload.expr_stmt.expression);
            if (exprRes.reg != 0) {
                emitOp(vm::OPCODE::OP_MOVE, 0, exprRes.reg, line, column);
            }
            freeIfTemp(exprRes);
        } else if (childNode.type >= NodeType::BinaryExpr && childNode.type <= NodeType::ImplicitCastExpr) {
            // 向后兼容：如果由于某种原因 Parser 丢出了裸表达式
            uint32_t line = currentPool->locations[child.index].line;
            uint32_t column = currentPool->locations[child.index].column;
            ExprResult res = compileExpression(child);
            if (i == declarations.size() - 1) {
                if (res.reg != 0) {
                    emitOp(vm::OPCODE::OP_MOVE, 0, res.reg, line, column);
                }
            }
            freeIfTemp(res);
        } else {
            compileNode(child);
        }
    }
}

void Compiler::compileSystemDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileComponentDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileFlowDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileKitsDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileTagDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileTagGroupDecl(ASTNodeIndex nodeIdx) {}

// 程序根与错误
void Compiler::compileProgramRoot(ASTNodeIndex nodeIdx) { compileModuleDecl(nodeIdx); }
void Compiler::compileErrorNode(ASTNodeIndex nodeIdx) {}

} // namespace niki::syntax
