#include "niki/syntax/compiler.hpp"
#include "niki/syntax/ast.hpp"

#include "niki/syntax/token.hpp"
#include "niki/vm/chunk.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <span>
#include <string>
#include <utility>


namespace niki::syntax {

std::expected<niki::Chunk, CompileResultError> Compiler::compile(const ASTPool &pool, ASTNodeIndex root,
                                                                 niki::Chunk initial_chunk) {
    if (!root.isvalid()) {
        return std::unexpected(CompileResultError{{{0, 0, "Invalid AST root node."}}});
    }

    currentPool = &pool;
    hadError = false;
    errorPool.clear();

    // 移动（move）到编译器内部。我们将其封装在一个顶层objfunction�?
    niki::vm::ObjFunction *topLevelFunc = new niki::vm::ObjFunction();
    niki::vm::ObjFunction();
    topLevelFunc->name = "<script>";
    topLevelFunc->chunk = std::move(initial_chunk);
    topLevelFunc->chunk.code.clear();
    topLevelFunc->chunk.constants.clear();
    pushContext(topLevelFunc);
    // 执行编译...
    compileNode(root);
    emitOp(vm::OPCODE::OP_RETURN, 0, 0);
    CompilerContext topContext = popContext();
    currentPool = nullptr;

    if (hadError) {
        delete topContext.function;
        return std::unexpected(CompileResultError{std::move(errorPool)});
    }

    // 编译成功！再次通过 Move 语义，把填满数据�?Chunk 移交出去�?
    niki::Chunk final_chunk = std::move(topContext.function->chunk);
    delete topContext.function;
    return std::move(final_chunk);
}

void Compiler::pushContext(niki::vm::ObjFunction *func) {
    // 保存当前状态（若有�?
    if (compilingFunction != nullptr) {
        contextStack.push_back({compilingFunction, compilingChunk, regAlloc, locals, scopeDepth, loop_stack});
    }
    // 切换到新上下�?
    compilingFunction = func;
    compilingChunk = &func->chunk;

    // 初始化新状�?
    regAlloc.reset();
    locals.clear();
    scopeDepth = 0;
    loop_stack.clear();
}

Compiler::CompilerContext Compiler::popContext() {
    CompilerContext current{compilingFunction, compilingChunk, regAlloc, locals, scopeDepth, loop_stack};
    if (!contextStack.empty()) {
        // 恢复上层状�?
        CompilerContext parent = contextStack.back();
        contextStack.pop_back();

        compilingFunction = parent.function;
        compilingChunk = parent.chunk;
        regAlloc = parent.regAlloc;
        locals = parent.locals;
        scopeDepth = parent.scopeDepth;
        loop_stack = parent.loop_stack;
    } else {
        compilingFunction = nullptr;
        compilingChunk = nullptr;
    }
    return current;
}
//---辅助函数---
void Compiler::freeIfTemp(const ExprResult &res) {
    if (res.is_temp) {
        regAlloc.free(res.reg);
    }
}

uint16_t Compiler::makeConstant(vm::Value value, uint32_t line, uint32_t column) {
    compilingChunk->constants.push_back(value);

    size_t index = compilingChunk->constants.size() - 1;

    if (index > 65535) {
        reportError(line, column, "Too many constants in one chunk.");
        return 0;
    }
    return static_cast<uint16_t>(index);
}

void Compiler::endScope() {
    scopeDepth--;
    while (locals.size() > 0 && locals.back().depth > scopeDepth) {
        regAlloc.free(locals.back().reg);
        locals.pop_back();
    }
}

uint8_t Compiler::resolveLocal(uint32_t name_id, uint32_t line, uint32_t column) {
    for (int i = locals.size() - 1; i >= 0; i--) {
        if (locals[i].name_id == name_id) {
            return locals[i].reg;
        }
    }
    reportError(line, column, "Undeclared variable.");
    return 0;
}
//---跳转相关---

size_t Compiler::currentCodePos() const { return compilingChunk->code.size(); };
size_t Compiler::emitJump(vm::OPCODE op, uint32_t line, uint32_t column) {
    emitOp(op, line, column);
    size_t patch_pos = currentCodePos();
    emitByte(0xFF, line, column);
    emitByte(0xFF, line, column);
    return patch_pos;
};
size_t Compiler::emitJump(vm::OPCODE op, uint8_t condReg, uint32_t line, uint32_t column) {
    emitOp(op, condReg, line, column);
    size_t patch_pos = currentCodePos();
    emitByte(0xFF, line, column);
    emitByte(0xFF, line, column);
    return patch_pos;
};
void Compiler::patchJump(size_t patch_pos, size_t target_pos) {
    size_t base = patch_pos + 2;
    if (target_pos < base) {
        reportError(0, 0, "Invalid forward jump target");
        return;
    }

    size_t offset = target_pos - base;
    if (offset > 0xFFFF) {
        reportError(0, 0, "Jump offest too large.");
        return;
    }
    compilingChunk->code[patch_pos] = static_cast<uint8_t>((offset >> 8) & 0xFF);
    compilingChunk->code[patch_pos + 1] = static_cast<uint8_t>(offset & 0xFF);
};
void Compiler::emitLoopBack(vm::OPCODE op, size_t target_pos, uint32_t line, uint32_t column) {
    emitOp(op, line, column);
    size_t base = currentCodePos() + 2;
    if (base < target_pos) {
        reportError(line, column, "Invalid backward jump target.");
        emitByte(0, line, column);
        emitByte(0, line, column);
        return;
    }

    size_t offset = base - target_pos;
    if (offset > 0xFFFF) {
        reportError(line, column, "Loop jump offset too large.");
        emitByte(0, line, column);
        emitByte(0, line, column);
        return;
    }

    emitByte(static_cast<uint8_t>((offset >> 8) & 0xFF), line, column);
    emitByte(static_cast<uint8_t>(offset & 0xFF), line, column);
};

void Compiler::beginLoop(size_t start_pos) { loop_stack.push_back({start_pos, {}}); };
void Compiler::addBreakPatch(size_t patch_pos, uint32_t line, uint32_t column) {
    if (loop_stack.empty()) {
        reportError(line, column, "break outside loop.");
        return;
    }
    loop_stack.back().break_patches.push_back(patch_pos);
};
void Compiler::endLoop(size_t loop_end_pos) {
    if (loop_stack.empty()) {
        return;
    }
    LoopContext ctx = loop_stack.back();
    loop_stack.pop_back();

    for (size_t p : ctx.break_patches) {
        patchJump(p, loop_end_pos);
    }
};
//---字节码发射器---
void Compiler::emitByte(uint8_t byte, uint32_t line, uint32_t column) {
    compilingChunk->code.push_back(byte);
    compilingChunk->lines.push_back(line);
    compilingChunk->columns.push_back(column);
}

void Compiler::emitOp(vm::OPCODE op, uint32_t line, uint32_t column) {
    emitByte(static_cast<uint8_t>(op), line, column);
}

void Compiler::emitOp(vm::OPCODE op, uint8_t a, uint32_t line, uint32_t column) {
    emitByte(static_cast<uint8_t>(op), line, column);
    emitByte(a, line, column);
}

void Compiler::emitOp(vm::OPCODE op, uint8_t a, uint8_t b, uint32_t line, uint32_t column) {
    emitByte(static_cast<uint8_t>(op), line, column);
    emitByte(a, line, column);
    emitByte(b, line, column);
}

void Compiler::emitOp(vm::OPCODE op, uint8_t a, uint8_t b, uint8_t c, uint32_t line, uint32_t column) {
    emitByte(static_cast<uint8_t>(op), line, column);
    emitByte(a, line, column);
    emitByte(b, line, column);
    emitByte(c, line, column);
}
void Compiler::emitOp(vm::OPCODE op, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint32_t line, uint32_t column) {
    emitByte(static_cast<uint8_t>(op), line, column);
    emitByte(a, line, column);
    emitByte(b, line, column);
    emitByte(c, line, column);
    emitByte(d, line, column);
}

void Compiler::emitConstant(vm::Value value, uint8_t targetReg, uint32_t line, uint32_t column) {
    uint16_t index = makeConstant(value, line, column);
    if (index < 255) {
        emitOp(vm::OPCODE::OP_LOAD_CONST, targetReg, static_cast<uint8_t>(index), line, column);
    } else {
        uint8_t high_byte = static_cast<uint8_t>((index >> 8) & 0xFF);
        uint8_t low_byte = static_cast<uint8_t>(index & 0xFF);
        emitOp(vm::OPCODE::OP_LOAD_CONST_W, targetReg, high_byte, low_byte, column);
    }
}

//---AST遍历核心(Visitor)---
void Compiler::compileNode(ASTNodeIndex nodeIdx) {
    if (!nodeIdx.isvalid()) {
        return;
    }
    const ASTNode &node = currentPool->getNode(nodeIdx);

    switch (node.type) {
    // ==== Expressions (独立且可能在顶层被直接求值丢�? ====
    case NodeType::BinaryExpr:
    case NodeType::UnaryExpr:
    case NodeType::LiteralExpr:
    case NodeType::IdentifierExpr:
    case NodeType::ArrayExpr:
    case NodeType::MapExpr:
    case NodeType::IndexExpr:
    case NodeType::CallExpr:
    case NodeType::MemberExpr:
    case NodeType::DispatchExpr:
    case NodeType::ClosureExpr:
    case NodeType::AwaitExpr:
    case NodeType::BorrowExpr:
    case NodeType::WildcardExpr:
    case NodeType::ImplicitCastExpr: {
        ExprResult res = compileExpression(nodeIdx);
        freeIfTemp(res); // 顶层表达式不赋值给任何东西，求值后直接销�?
        break;
    }

    // ==== Statements (语句直接调用语句编译模块) ====
    case NodeType::ExpressionStmt:
    case NodeType::AssignmentStmt:
    case NodeType::VarDeclStmt:
    case NodeType::ConstDeclStmt:
    case NodeType::BlockStmt:
    case NodeType::IfStmt:
    case NodeType::LoopStmt:
    case NodeType::MatchStmt:
    case NodeType::MatchCaseStmt:
    case NodeType::ContinueStmt:
    case NodeType::BreakStmt:
    case NodeType::ReturnStmt:
    case NodeType::NockStmt:
    case NodeType::AttachStmt:
    case NodeType::DetachStmt:
    case NodeType::TargetStmt: {
        compileStatement(nodeIdx);
        break;
    }

    // ==== Declarations (其他顶层声明) ====
    // ... 如果有专门的声明NodeType可在此补�?
    default:
        // 兜底进入声明处理
        compileDeclaration(nodeIdx);
        break;
    }
}

//---错误与警告处�?--
void Compiler::reportError(const Token &token, std::string_view message) {}

void Compiler::reportError(uint32_t line, uint32_t column, std::string_view message) {
    errorPool.push_back({line, column, std::string(message)});
    hadError = true;
}

void Compiler::reportWarning(uint32_t line, uint32_t column, std::string_view message) {
    // �?Warning 直接输出到控制台，不计入 errorPool 也不标记 hadError
    std::cerr << "[Compiler Warning] line " << line << ":" << column << " - " << message << std::endl;
}

} // namespace niki::syntax
