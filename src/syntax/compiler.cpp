#include "niki/syntax/compiler.hpp"
#include "niki/syntax/ast.hpp"

#include "niki/syntax/token.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

using namespace niki::syntax;
std::expected<niki::Chunk, CompileResultError> Compiler::compile(const ASTPool &pool, ASTNodeIndex root,
                                                                 niki::Chunk initial_chunk) {
    if (!root.isvalid()) {
        return std::unexpected(CompileResultError{{{0, 0, "Invalid AST root node."}}});
    }

    currentPool = &pool;
    hadError = false;
    errorPool.clear();

    // 核心动作：把外部传进来的（可能已经 reserve 过内存的）Chunk
    // 移动（Move）到编译器内部。这里没有任何深拷贝，只有指针的转移。
    compilingChunk = std::move(initial_chunk);
    // 确保它是干净的，但保留了其底层 capacity
    compilingChunk.code.clear();
    compilingChunk.constants.clear();

    // 执行编译...
    compileNode(root);
    emitOp(vm::OPCODE::OP_RETURN, 0, 0);

    currentPool = nullptr;

    if (hadError) {
        // 如果失败，这个 compilingChunk 就随风消散了，它的内存会被正常回收。
        return std::unexpected(CompileResultError{std::move(errorPool)});
    }

    // 编译成功！再次通过 Move 语义，把填满数据的 Chunk 移交出去。
    return std::move(compilingChunk);
}
//---辅助函数---
void Compiler::freeIfTemp(const ExprResult &res) {
    if (res.is_temp) {
        regAlloc.free(res.reg);
    }
}
uint16_t Compiler::makeConstant(vm::Value value, uint32_t line, uint32_t column) {
    compilingChunk.constants.push_back(value);

    size_t index = compilingChunk.constants.size() - 1;

    if (index > 65535) {
        // 在未来的工业级实现中，这里应该返回一个特殊的标识，
        // 让调用者知道必须改用 OP_LOAD_CONST_W (宽指令)。
        // 但目前，我们直接硬阻断。
        reportError(line, column, "Too many constants in one chunk.");
        return 0;
    }
    return static_cast<uint16_t>(index);
};

void Compiler::endScope() {
    scopeDepth--;
    while (locals.size() > 0 && locals.back().depth > scopeDepth) {
        regAlloc.free(locals.back().reg);
        locals.pop_back();
    }
};
uint8_t Compiler::resolveLocal(uint32_t name_id, uint32_t line, uint32_t column) {
    for (int i = locals.size() - 1; i >= 0; i--) {
        if (locals[i].name_id == name_id) {
            return locals[i].reg;
        }
    }
    reportError(line, column, "Undeclared variable.");
    return 0;
};

//---字节码发射器---
void Compiler::emitByte(uint8_t byte, uint32_t line, uint32_t column) {
    compilingChunk.code.push_back(byte);
    compilingChunk.lines.push_back(line);
    compilingChunk.columns.push_back(column);
};
void Compiler::emitOp(vm::OPCODE op, uint32_t line, uint32_t column) {
    emitByte(static_cast<uint8_t>(op), line, column);
};
void Compiler::emitOp(vm::OPCODE op, uint8_t a, uint32_t line, uint32_t column) {
    emitByte(static_cast<uint8_t>(op), line, column);
    emitByte(a, line, column);
};
void Compiler::emitOp(vm::OPCODE op, uint8_t a, uint8_t b, uint32_t line, uint32_t column) {
    emitByte(static_cast<uint8_t>(op), line, column);
    emitByte(a, line, column);
    emitByte(b, line, column);
};
void Compiler::emitOp(vm::OPCODE op, uint8_t a, uint8_t b, uint8_t c, uint32_t line, uint32_t column) {
    emitByte(static_cast<uint8_t>(op), line, column);
    emitByte(a, line, column);
    emitByte(b, line, column);
    emitByte(c, line, column);
};
void Compiler::emitConstant(vm::Value value, uint8_t targetReg, uint32_t line, uint32_t column) {
    uint16_t index = makeConstant(value, line, column);
    if (index < 255) {
        emitOp(vm::OPCODE::OP_LOAD_CONST, targetReg, static_cast<uint8_t>(index), line, column);
    } else {
        uint8_t high_byte = static_cast<uint8_t>((index >> 8) & 0xFF);
        uint8_t low_byte = static_cast<uint8_t>(index & 0xFF);
        emitOp(vm::OPCODE::OP_LOAD_CONST_W, targetReg, high_byte, low_byte, column);
    }
};

//---AST遍历核心(Visitor)---
ExprResult Compiler::compileNode(ASTNodeIndex nodeIdx) {
    if (!nodeIdx.isvalid()) {
        return {};
    }
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    switch (node.type) {
    case NodeType::BinaryExpr:
        return compileBinaryExpr(nodeIdx);
        break;
    case NodeType::LiteralExpr:
        compileLiteralExpr(nodeIdx);
        break;

    // todo:表达式先到这里,剩下的后面再写
    case NodeType::ExpressionStmt:
        compileExpressionStmt(nodeIdx);
        break;
    default:
        reportError(line, column, "Unknown AST node type during compilation.");
        return {};
    }
};
//---表达式编译---
ExprResult Compiler::compileExpression(ASTNodeIndex exprIdx) {
    if (!exprIdx.isvalid()) {
        return {};
    }

    const ASTNode &node = currentPool->getNode(exprIdx);
    uint32_t line = currentPool->locations[exprIdx.index].line;
    uint32_t column = currentPool->locations[exprIdx.index].column;
    switch (node.type) {
    case NodeType::BinaryExpr:
        return compileBinaryExpr(exprIdx);
    case NodeType::UnaryExpr:
        return compileUnaryExpr(exprIdx);
    case NodeType::LiteralExpr:
        return compileLiteralExpr(exprIdx);
    // ...
    default:
        reportError(line, column, "Expected an expression.");
        return ExprResult{0, true};
    }
};

void Compiler::compileExpressionWithTarget(ASTNodeIndex exprIdx, uint8_t targetReg) {
    if (!exprIdx.isvalid()) {
        return;
    }
    const ASTNode &node = currentPool->getNode(exprIdx);
    uint32_t line = currentPool->locations[exprIdx.index].line;
    uint32_t column = currentPool->locations[exprIdx.index].column;
    if (node.type == NodeType::LiteralExpr) {
        switch (node.payload.literal.literal_type) {
        case TokenType::LITERAL_INT: { // 此处未来可以优化，减少一次搬运
            uint32_t pool_idx = node.payload.literal.const_pool_index;
            vm::Value val = currentPool->constants[pool_idx];
            uint8_t chunk_idx = makeConstant(val, line, column);
            emitOp(vm::OPCODE::OP_LOAD_CONST, targetReg, chunk_idx, line, column);
            return;
        }
        case TokenType::LITERAL_FLOAT:
        case TokenType::LITERAL_STRING: {
            uint32_t pool_idx = node.payload.literal.const_pool_index;
            vm::Value val = currentPool->constants[pool_idx];
            uint8_t chunk_idx = makeConstant(val, line, column);
            emitOp(vm::OPCODE::OP_LOAD_CONST, targetReg, chunk_idx, line, column);
            return;
        }
        case TokenType::KEYWORD_TRUE:
            emitOp(vm::OPCODE::OP_TRUE, targetReg, line, column);
            return;
        case TokenType::KEYWORD_FALSE:
            emitOp(vm::OPCODE::OP_FALSE, targetReg, line, column);
            return;
        case TokenType::NIL:
            emitOp(vm::OPCODE::OP_NIL, targetReg, line, column);
            return;
        default:
            reportError(line, column, "Expected an expression.");
        }
    }
    ExprResult resultReg = compileExpression(exprIdx);
    if (resultReg.reg != targetReg) {
        emitOp(vm::OPCODE::OP_MOVE, targetReg, resultReg.reg, line, column);
    }
    freeIfTemp(resultReg);
};

// 基础计算
ExprResult Compiler::compileBinaryExpr(ASTNodeIndex nodeIdx) {
    if (!nodeIdx.isvalid()) {
        return {};
    }
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    ExprResult leftReg = compileExpression(node.payload.binary.left);
    ExprResult rightReg = compileExpression(node.payload.binary.right);
    ExprResult resultReg = {regAlloc.allocate(), true};

    switch (node.payload.binary.op) {
    case TokenType::SYM_PLUS:
        emitOp(vm::OPCODE::OP_IADD, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_MINUS:
        emitOp(vm::OPCODE::OP_ISUB, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_STAR:
        emitOp(vm::OPCODE::OP_IMUL, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_SLASH:
        emitOp(vm::OPCODE::OP_IDIV, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_MOD:
        emitOp(vm::OPCODE::OP_IMOD, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_EQUAL_EQUAL:
        emitOp(vm::OPCODE::OP_IEQ, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_BANG_EQUAL:
        emitOp(vm::OPCODE::OP_INE, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_GREATER:
        emitOp(vm::OPCODE::OP_IGT, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    case TokenType::SYM_LESS:
        emitOp(vm::OPCODE::OP_ILT, resultReg.reg, leftReg.reg, rightReg.reg, line, column);
        break;
    default:
        reportError(line, column, "Unknown binary operator.");
        break;
    }
    freeIfTemp(leftReg);
    freeIfTemp(rightReg);
    return ExprResult{resultReg.reg, true};
};
ExprResult Compiler::compileUnaryExpr(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    ExprResult reg = compileExpression(node.payload.unary.operand);
    ExprResult resultReg = {regAlloc.allocate(), true};

    switch (node.payload.unary.op) {
    case TokenType::SYM_MINUS:
        emitOp(vm::OPCODE::OP_NEG, resultReg.reg, reg.reg, line, column);
        break;
    case TokenType::SYM_BANG:
        emitOp(vm::OPCODE::OP_NOT, resultReg.reg, reg.reg, line, column);
        break;
    case TokenType::SYM_BIT_NOT:
        emitOp(vm::OPCODE::OP_BIT_NOT, resultReg.reg, reg.reg, line, column);
        break;
    default:
        reportError(line, column, "Unknown unary operator.");
        break;
    }
    freeIfTemp(reg);
    return ExprResult{resultReg.reg, true};
};
ExprResult Compiler::compileLiteralExpr(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    ExprResult resultReg = {regAlloc.allocate(), true};

    switch (node.payload.literal.literal_type) {
    case TokenType::LITERAL_INT:
    case TokenType::LITERAL_FLOAT:
    case TokenType::LITERAL_STRING: {
        uint32_t pool_idx = node.payload.literal.const_pool_index;
        vm::Value val = currentPool->constants[pool_idx];
        uint8_t chunk_idx = makeConstant(val, line, column);
        emitOp(vm::OPCODE::OP_LOAD_CONST, resultReg.reg, chunk_idx, line, column);
        break;
    }
    case TokenType::KEYWORD_TRUE:
        emitOp(vm::OPCODE::OP_TRUE, resultReg.reg, line, column);
        break;
    case TokenType::KEYWORD_FALSE:
        emitOp(vm::OPCODE::OP_FALSE, resultReg.reg, line, column);
        break;
    case TokenType::NIL:
        emitOp(vm::OPCODE::OP_NIL, resultReg.reg, line, column);
        break;
    default:
        reportError(line, column, "Unknow literal type.");
        break;
    }
    return ExprResult{resultReg.reg, true};
};
ExprResult Compiler::compileIdentifierExpr(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;
    uint8_t reg = resolveLocal(node.payload.identifier.name_id, line, column);
    return ExprResult{reg, false};
};
// 复杂数据结构
ExprResult Compiler::compileArrayExpr(ASTNodeIndex nodeIdx) {};
ExprResult Compiler::compileMapExpr(ASTNodeIndex nodeIdx) {};
ExprResult Compiler::compileIndexExpr(ASTNodeIndex nodeIdx) {};
// 对象与方法
ExprResult Compiler::compileCallExpr(ASTNodeIndex nodeIdx) {};
ExprResult Compiler::compileMemberExpr(ASTNodeIndex nodeIdx) {};
ExprResult Compiler::compileDispatchExpr(ASTNodeIndex nodeIdx) {};
// 闭包与高级特性
ExprResult Compiler::compileClosureExpr(ASTNodeIndex nodeIdx) {};
ExprResult Compiler::compileAwaitExpr(ASTNodeIndex nodeIdx) {};

//---语句编译---
void Compiler::compileStatement(ASTNodeIndex stmtIdx) {
    if (!stmtIdx.isvalid()) {
        return;
    }
    const ASTNode &node = currentPool->getNode(stmtIdx);
    uint32_t line = currentPool->locations[stmtIdx.index].line;
    uint32_t column = currentPool->locations[stmtIdx.index].column;
    switch (node.type) {
    case NodeType::ExpressionStmt:
        compileExpressionStmt(stmtIdx);
        break;
    // ...
    default:
        reportError(line, column, "Expected a statement.");
        break;
    }
};
// 基础语句
void Compiler::compileExpressionStmt(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;

    ExprResult resultReg = compileExpression(node.payload.expr_stmt.expression);

    freeIfTemp(resultReg);
};
void Compiler::compileAssignmentStmt(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;
    ASTNodeIndex targetIdx = node.payload.assign_stmt.target;
    if (!targetIdx.isvalid()) {
        return;
    }

    const ASTNode &targetNode = currentPool->getNode(targetIdx);
    if (targetNode.type == NodeType::IdentifierExpr) {
        uint32_t name_id = targetNode.payload.identifier.name_id;
        uint8_t targetReg = resolveLocal(name_id, line, column);
        TokenType op = node.payload.assign_stmt.op;

        if (op == TokenType::SYM_EQUAL) {
            compileExpressionWithTarget(node.payload.assign_stmt.value, targetReg);
        } else {
            ExprResult rightRes = compileExpression(node.payload.assign_stmt.value);

            switch (op) {
            case TokenType::SYM_PLUS_EQUAL:
                emitOp(vm::OPCODE::OP_IADD, targetReg, targetReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_MINUS_EQUAL:
                emitOp(vm::OPCODE::OP_ISUB, targetReg, targetReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_STAR_EQUAL:
                emitOp(vm::OPCODE::OP_IMUL, targetReg, targetReg, rightRes.reg, line, column);
                break;
            case TokenType::SYM_SLASH_EQUAL:
                emitOp(vm::OPCODE::OP_IDIV, targetReg, targetReg, rightRes.reg, line, column);
                break;
            // ... 可以继续扩展位运算等复合赋值 ...
            default:
                reportError(line, column, "Unsupported assignment operator.");
                break;
            }
            freeIfTemp(rightRes);
        }
    } else {
        reportError(line, column, "Invalid assignment target. Only simple variables are supported currently.");
    }
    uint8_t targetReg = resolveLocal(node.payload.assign_stmt.target, line, column);

    compileExpressionWithTarget(node.payload.assign_stmt.value, targetReg);
};
void Compiler::compileVarDeclStmt(ASTNodeIndex nodeIdx) {
    const ASTNode &node = currentPool->getNode(nodeIdx);
    uint32_t line = currentPool->locations[nodeIdx.index].line;
    uint32_t column = currentPool->locations[nodeIdx.index].column;
    uint32_t name_id = node.payload.var_decl.name_id;
    // 【修正】：只在当前作用域深度查找是否重复声明！
    for (int i = (int)locals.size() - 1; i >= 0; i--) {
        // 如果已经退到了外层作用域，就不用查了，因为允许 Shadowing
        if (locals[i].depth != scopeDepth)
            break;

        if (locals[i].name_id == name_id) {
            reportError(line, column, "Variable already declared in this scope.");
            break; // 报错后跳出
        }
    }

    uint8_t varReg = regAlloc.allocate();
    locals.push_back({name_id, varReg, scopeDepth});

    if (node.payload.var_decl.init_expr.isvalid()) {
        compileExpressionWithTarget(node.payload.var_decl.init_expr, varReg);
    } else {
        emitOp(vm::OPCODE::OP_NIL, varReg, line, column);
    }
};
void Compiler::compileConstDeclStmt(ASTNodeIndex nodeIdx) {};
void Compiler::compileBlockStmt(ASTNodeIndex stmtIdx) {
    const ASTNode &node = currentPool->getNode(stmtIdx);
    uint32_t line = currentPool->locations[stmtIdx.index].line;
    uint32_t column = currentPool->locations[stmtIdx.index].column;
    beginScope();

    std::span<const ASTNodeIndex> stmts = currentPool->get_list(node.payload.block.statements);
    for (ASTNodeIndex stmt : stmts) {
        compileStatement(stmt);
    }
    endScope();
};
// 控制流
void Compiler::compileIfStmt(ASTNodeIndex nodeIdx) {};
void Compiler::compileLoopStmt(ASTNodeIndex nodeIdx) {};
void Compiler::compileMatchStmt(ASTNodeIndex nodeIdx) {};
void Compiler::compileMatchCase(ASTNodeIndex nodeIdx) {};
// 跳转与中断
void Compiler::compileContinueStmt(ASTNodeIndex nodeIdx) {};
void Compiler::compileBreakStmt(ASTNodeIndex nodeIdx) {};
void Compiler::compileReturnStmt(ASTNodeIndex nodeIdx) {};
void Compiler::compileNockStmt(ASTNodeIndex nodeIdx) {};
// 组件挂载与卸载
void Compiler::compileAttachStmt(ASTNodeIndex nodeIdx) {};
void Compiler::compileDetachStmt(ASTNodeIndex nodeIdx) {};
void Compiler::compileTargetStmt(ASTNodeIndex nodeIdx) {};
// 异常处理
void Compiler::compileThrowStmt(ASTNodeIndex nodeIdx) {};
void Compiler::compileTryCatchStmt(ASTNodeIndex nodeIdx) {};
//---顶层声明编译---
void Compiler::compileDeclaration(ASTNodeIndex nodeIdx) {};
void Compiler::compileKitsDecl(ASTNodeIndex nodeIdx) {};
// todo: 其他声明类型编译
//---错误处理---
void Compiler::reportError(const Token &token, std::string_view message) {};
void Compiler::reportError(uint32_t line, uint32_t column, std::string_view message) {
    errorPool.push_back({line, column, std::string(message)});
    hadError = true;
};
