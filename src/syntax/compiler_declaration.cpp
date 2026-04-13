#include "niki/debug/logger.hpp"
#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>
#include <span>

namespace niki::syntax {

//---顶层声明编译---
void Compiler::compileDeclaration(ASTNodeIndex nodeIdx) {
    if (!nodeIdx.isvalid()) {
        return;
    }
    const auto &node = getNodeCtx(nodeIdx).node;
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;
    niki::debug::trace("compiler", "declaration entry type={} at {}:{}", toString(node.type), line, column);

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
void Compiler::compileFunctionDecl(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);

    uint32_t func_index = node.payload.func_decl.function_index;
    const FunctionData &func_data = currentPool->function_data[func_index];

    // 在外层作用域为该函数名分配一个虚拟寄存器（若它不是匿名函数）
    // 目前niki mvp将函数作为顶层声明

    // 创建一个新的objfuction，准备编译其内部字节码
    niki::vm::ObjFunction *funcObj = new niki::vm::ObjFunction();

    std::span<const ASTNodeIndex> paramNodes = currentPool->get_list(func_data.params);
    funcObj->arity = paramNodes.size();

    // 压入编译
    // 此时编译器内部所有的locals、regalloc、compilingChunk都被替换为了这个新的函数环境
    pushContext(funcObj);

    // 为参数分配局部变量
    // 在niki的调用约定中，vm在opcall时会将参数映射到被调用者的0~n寄存器
    // 所以编译器必须保证参数分配的虚拟寄存器刚好是0，1，2，...
    for (ASTNodeIndex paramNodeIdx : paramNodes) {
        const ASTNode &paramNode = currentPool->getNode(paramNodeIdx);
        uint32_t param_name_id = paramNode.payload.identifier.name_id;

        uint8_t paramReg = regAlloc.allocate();
        locals.push_back({param_name_id, paramReg, scopeDepth});
    }

    // 编译函数
    if (func_data.body.isvalid()) {
        compileStatement(func_data.body);
    }

    // 隐式return兜底
    // 如果函数没有显式写return，默认返回nil（写入寄存器0，然后执行OP_return
    emitOp(vm::OPCODE::OP_NIL, 0, line, column);
    emitOp(vm::OPCODE::OP_RETURN, line, column);

    // 弹出编译上下文，恢复到外层环境
    CompilerContext context = popContext();

    // 在外层作用域为该函数名分配一个虚拟寄存器（若它不是匿名函数）
    // 注意：必须在 popContext 之后进行分配push_back，这样变量才会正确地注册到外层的 locals 表中
    uint8_t funcReg = regAlloc.allocate();
    locals.push_back({func_data.name_id, funcReg, scopeDepth});

    // 将编译好 objfuction作为一个常量写入到外层常量池中
    vm::Value funcValue = vm::Value::makeObject(context.function);
    uint16_t constIdx = makeConstant(funcValue, line, column);

    // 将常量池中的函数对象，加载到我们第一步分配的funcReg
    if (constIdx < 255) {
        emitOp(vm::OPCODE::OP_LOAD_CONST, funcReg, static_cast<uint8_t>(constIdx), line, column);

    } else {
        uint8_t high_byte = static_cast<uint8_t>((constIdx >> 8) & 0xFF);
        uint8_t low_byte = static_cast<uint8_t>(constIdx & 0xFF);
        emitOp(vm::OPCODE::OP_LOAD_CONST_W, funcReg, high_byte, low_byte, line, column);
    }
}
void Compiler::compileInterfaceMethod(ASTNodeIndex nodeIdx) {}
void Compiler::compileStructDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileEnumDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileTypeAliasDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileInterfaceDecl(ASTNodeIndex nodeIdx) {}
void Compiler::compileImplDecl(ASTNodeIndex nodeIdx) {}

// NIKI特有
void Compiler::compileModuleDecl(ASTNodeIndex nodeIdx) {
    const auto &node = getNodeCtx(nodeIdx).node;
    const auto &bodyNode = getNodeCtx(node.payload.module_decl.body).node;
    std::span<const ASTNodeIndex> declarations = currentPool->get_list(bodyNode.payload.list.elements);
    for (size_t i = 0; i < declarations.size(); ++i) {
        ASTNodeIndex child = declarations[i];
        auto [childNode, line, column] = getNodeCtx(child);

        // 针对 REPL 最小回路：如果我们遇到的是最后一条表达式语句（ExpressionStmt）
        if (childNode.type == NodeType::ExpressionStmt && i == declarations.size() - 1) {
            // 取出里面真正的表达式
            ExprResult exprRes = compileExpression(childNode.payload.expr_stmt.expression);
            if (exprRes.reg != 0) {
                emitOp(vm::OPCODE::OP_MOVE, 0, exprRes.reg, line, column);
            }
            freeIfTemp(exprRes);
        } else if (childNode.type >= NodeType::BinaryExpr && childNode.type <= NodeType::ImplicitCastExpr) {
            // 向后兼容：如果由于某种原�?Parser 丢出了裸表达�?
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
