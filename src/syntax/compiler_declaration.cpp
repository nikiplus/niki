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
// 入口：按 NodeType 路由到 compileXXXDecl，并配合 preCompile 实现两遍流程。
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
    case NodeType::StructDecl:
        // 在第二遍扫描时，直接跳过函数和结构体声明，因为在 preCompile 时已经发射过了
        break;
    case NodeType::InterfaceMethod:
        compileInterfaceMethod(nodeIdx);
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
    // 流程：创建 ObjFunction -> 参数入寄存器窗口 -> 编译函数体 -> 注册全局符号。
    auto [node, line, column] = getNodeCtx(nodeIdx);

    uint32_t func_index = node.payload.func_decl.function_index;
    const FunctionData &func_data = currentPool->function_data[func_index];

    // 在外层作用域为该函数名分配一个虚拟寄存器（若它不是匿名函数）
    // 目前niki mvp将函数作为顶层声明

    // 创建一个新的objfuction，准备编译其内部字节码
    // 注意：ObjFunction 现在包含了 Object object_header，必须正确初始化以避免野指针和强转崩溃
    niki::vm::ObjFunction *funcObj = new niki::vm::ObjFunction();
    funcObj->object_header.type = vm::ObjType::Function;
    funcObj->object_header.isMarked = false;
    funcObj->name_id = func_data.name_id; // Initialize name_id from FunctionData

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
        uint32_t param_name_id = paramNode.payload.var_decl.name_id;

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

    // 将编译好的 objfuction作为一个常量写入到外层常量池中
    vm::Value funcValue = vm::Value::makeObject(context.function);
    uint16_t constIdx = makeConstant(funcValue, line, column);

    // 发射 OP_DEFINE_GLOBAL，把函数注册到 VM 的全局表中！
    // 不再占用外层的局部变量和寄存器
    if (constIdx < 255) {
        emitOp(vm::OPCODE::OP_DEFINE_GLOBAL, static_cast<uint8_t>(constIdx), line, column);
    } else {
        // 这里理论上需要一个 OP_DEFINE_GLOBAL_W，MVP 暂时不写长指令了
        // 我们假设常量池不会超过 255 个函数
        emitOp(vm::OPCODE::OP_DEFINE_GLOBAL, static_cast<uint8_t>(constIdx), line, column);
    }
}
void Compiler::compileInterfaceMethod(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Interface method compilation is not implemented yet.");
}
void Compiler::compileStructDecl(ASTNodeIndex nodeIdx) {
    // 流程：创建 ObjStructDef -> 写入常量池 -> OP_DEFINE_GLOBAL 注册全局。
    auto [node, line, column] = getNodeCtx(nodeIdx);
    
    uint32_t struct_idx = node.payload.struct_decl.struct_index;
    const StructData &struct_data = currentPool->struct_data[struct_idx];

    std::span<const ASTNodeIndex> fields = currentPool->get_list(struct_data.names);
    uint32_t field_count = static_cast<uint32_t>(fields.size());

    // 1. 在编译期创建一个结构体蓝图对象
    vm::ObjStructDef *structDef = vm::allocateStructDef(struct_data.name_id, field_count);
    
    // 2. 将其放入当前 chunk 的常量池
    vm::Value defValue = vm::Value::makeObject(structDef);
    uint16_t constIdx = makeConstant(defValue, line, column);

    // 3. 发射 OP_DEFINE_GLOBAL，把蓝图注册到 VM 的全局表中
    if (constIdx < 255) {
        emitOp(vm::OPCODE::OP_DEFINE_GLOBAL, static_cast<uint8_t>(constIdx), line, column);
    } else {
        emitOp(vm::OPCODE::OP_DEFINE_GLOBAL_W, static_cast<uint8_t>((constIdx >> 8) & 0xFF), static_cast<uint8_t>(constIdx & 0xFF), line, column);
    }
}
void Compiler::compileEnumDecl(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Enum declaration compilation is not implemented yet.");
}
void Compiler::compileTypeAliasDecl(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Type alias compilation is not implemented yet.");
}
void Compiler::compileInterfaceDecl(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Interface declaration compilation is not implemented yet.");
}
void Compiler::compileImplDecl(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Impl declaration compilation is not implemented yet.");
}

// NIKI特有
void Compiler::compileModuleDecl(ASTNodeIndex nodeIdx) {
    // Two-Pass：先预编译顶层符号，再编译其余声明/语句；末尾表达式支持 REPL 回显。
    const auto &node = getNodeCtx(nodeIdx).node;
    const auto &bodyNode = getNodeCtx(node.payload.module_decl.body).node;
    std::span<const ASTNodeIndex> declarations = currentPool->get_list(bodyNode.payload.list.elements);

    // 第一遍：把所有的顶层函数声明提前编译，发射 OP_DEFINE_GLOBAL
    for (size_t i = 0; i < declarations.size(); ++i) {
        preCompileNode(declarations[i]);
    }

    // 第二遍：编译剩下的所有非函数声明和执行语句
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

void Compiler::compileSystemDecl(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "System declaration compilation is not implemented yet.");
}
void Compiler::compileComponentDecl(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Component declaration compilation is not implemented yet.");
}
void Compiler::compileFlowDecl(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Flow declaration compilation is not implemented yet.");
}
void Compiler::compileKitsDecl(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Kits declaration compilation is not implemented yet.");
}
void Compiler::compileTagDecl(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Tag declaration compilation is not implemented yet.");
}
void Compiler::compileTagGroupDecl(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "TagGroup declaration compilation is not implemented yet.");
}

// 程序根与错误
void Compiler::compileProgramRoot(ASTNodeIndex nodeIdx) { compileModuleDecl(nodeIdx); }
void Compiler::compileErrorNode(ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    reportError(line, column, "Encountered parser error node during compilation.");
}

} // namespace niki::syntax
