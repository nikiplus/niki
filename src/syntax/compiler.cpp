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

namespace niki::syntax {

std::expected<niki::Chunk, CompileResultError> Compiler::compile(const ASTPool &pool, ASTNodeIndex root,
                                                                 niki::Chunk initial_chunk) {
    if (!root.isvalid()) {
        return std::unexpected(CompileResultError{{{0, 0, "Invalid AST root node."}}});
    }

    currentPool = &pool;
    hadError = false;
    errorPool.clear();
    regAlloc.reset();
    locals.clear();
    scopeDepth = 0;
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

size_t Compiler::currentCodePos() const { return compilingChunk.code.size(); };
size_t Compiler::emitJump(vm::OPCODE op, uint32_t line, uint32_t column) {
    emitOp(op, line, column);
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
    if (offset > 0XFFFF) {
        reportError(0, 0, "Jumpoffest too large.");
        return;
    }
    compilingChunk.code[patch_pos] = static_cast<uint8_t>((offset >> 8) & 0xFF);
    compilingChunk.code[patch_pos + 1] = static_cast<uint8_t>(offset & 0xFF);
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

void Compiler::beginLoop(size_t start_pos) { loop_stack.push_back({start_pos, {}, {}}); };
void Compiler::addBreakPatch(size_t patch_pos, uint32_t line, uint32_t column) {
    if (loop_stack.empty()) {
        reportError(line, column, "break outside loop.");
        return;
    }
    loop_stack.back().break_patches.push_back(patch_pos);
};
void Compiler::addContinuePatch(size_t patch_pos, uint32_t line, uint32_t column) {
    if (loop_stack.empty()) {
        reportError(line, column, "continue outside loop.");
        return;
    }
    loop_stack.back().continue_patches.push_back(patch_pos);
};
void Compiler::endLoop(size_t loop_end_pos) {
    if (loop_stack.empty()) {
        return;
    }
    LoopContext ctx = loop_stack.back();
    loop_stack.pop_back();

    for (size_t p : ctx.continue_patches) {
        patchJump(p, ctx.start_pos);
        for (size_t p : ctx.break_patches) {
            patchJump(p, loop_end_pos);
        }
    }
};
//---字节码发射器---
void Compiler::emitByte(uint8_t byte, uint32_t line, uint32_t column) {
    compilingChunk.code.push_back(byte);
    compilingChunk.lines.push_back(line);
    compilingChunk.columns.push_back(column);
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
    // ==== Expressions (独立且可能在顶层被直接求值丢弃) ====
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
        freeIfTemp(res); // 顶层表达式不赋值给任何东西，求值后直接销毁
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
    // ... 如果有专门的声明NodeType可在此补充
    default:
        // 兜底进入声明处理
        compileDeclaration(nodeIdx);
        break;
    }
}

//---错误处理---
void Compiler::reportError(const Token &token, std::string_view message) {}

void Compiler::reportError(uint32_t line, uint32_t column, std::string_view message) {
    errorPool.push_back({line, column, std::string(message)});
    hadError = true;
}

} // namespace niki::syntax