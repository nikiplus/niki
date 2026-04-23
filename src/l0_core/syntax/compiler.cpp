#include "niki/l0_core/syntax/compiler.hpp"

#include "niki/debug/logger.hpp"
#include "niki/l0_core/diagnostic/codes.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include "niki/l0_core/vm/chunk.hpp"
#include "niki/l0_core/vm/object.hpp"
#include "niki/l0_core/vm/opcode.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

namespace niki::syntax {

std::expected<Chunk, diagnostic::DiagnosticBag> Compiler::compile(const ASTPool &pool, ASTNodeIndex root,
                                                                  const std::vector<semantic::NKType> &typeTable,
                                                                  Chunk initial_chunk,
                                                                  const GlobalTypeArena *globalArena,
                                                                  const GlobalSymbolTable *globalSymbols) {
    // 编译主流程（顶层脚本上下文）：
    // A. 初始化编译状态与统计器
    // B. 建立 <script> 顶层函数并切换上下文
    // C. 遍历根节点发射字节码，最后补 OP_RETURN
    // D. 若无错误则导出 chunk + string_pool
    if (!root.isvalid()) {
        diagnostic::DiagnosticBag bag;
        bag.reportError(diagnostic::DiagnosticStage::Compiler, diagnostic::codes::compiler::InvalidRoot,
                        "Invalid AST root node.");
        return std::unexpected(std::move(bag));
    }

    currentPool = &pool;
    currentTypeTable = &typeTable;
    currentGlobalArena = globalArena;
    currentGlobalSymbols = globalSymbols;
    hadError = false;
    diagnostics = diagnostic::DiagnosticBag{};
    warningCount = 0;
    traceIndent = 0;
    opcodeEmitCount.fill(0);

    // 移动（move）到编译器内部。我们将其封装在一个顶层objfunction
    vm::ObjFunction *topLevelFunc = new vm::ObjFunction();
    topLevelFunc->name_id = const_cast<ASTPool *>(currentPool)->internString("<script>");
    topLevelFunc->chunk = std::move(initial_chunk);
    topLevelFunc->chunk.code.clear();
    topLevelFunc->chunk.constants.clear();
    pushContext(topLevelFunc);
    // 执行编译...
    compileNode(root);
    emitOp(vm::OPCODE::OP_RETURN, 0, 0);
    CompilerContext topContext = popContext();
    currentPool = nullptr;
    currentTypeTable = nullptr;
    currentGlobalArena = nullptr;
    currentGlobalSymbols = nullptr;

    if (hadError) {
        debug::debug("compiler", "compile failed, errors={}, warnings={}", diagnostics.size(), warningCount);
        delete topContext.function;
        return std::unexpected(std::move(diagnostics));
    }

    // 编译成功！再次通过 Move 语义，把填满数据的Chunk 移交出去
    Chunk final_chunk = std::move(topContext.function->chunk);

    // 使用 Driver 级共享 interner 快照，确保多模块 name_id 语义一致。
    final_chunk.string_pool = pool.snapshotStringPool();

    uint32_t totalOpCount = 0;
    for (uint32_t count : opcodeEmitCount) {
        totalOpCount += count;
    }
    debug::debug("compiler", "emit summary: ops={}, code_bytes={}, constants={}, warnings={}", totalOpCount,
                 final_chunk.code.size(), final_chunk.constants.size(), warningCount);
    delete topContext.function;
    return std::move(final_chunk);
}

void Compiler::pushContext(vm::ObjFunction *func) {
    // 进入新的函数编译上下文：
    // 保存上层状态，然后为新函数重置寄存器/locals/scope/loop 栈。
    // 保存当前状态（若有）
    if (compilingFunction != nullptr) {
        contextStack.push_back({compilingFunction, compilingChunk, regAlloc, locals, scopeDepth, loop_stack});
    }
    // 切换到新上下文
    compilingFunction = func;
    compilingChunk = &func->chunk;

    // 初始化新状态
    regAlloc.reset();
    locals.clear();
    scopeDepth = 0;
    loop_stack.clear();
}

Compiler::CompilerContext Compiler::popContext() {
    // 退出函数编译上下文：
    // 返回当前函数状态，并在存在父上下文时完整恢复父级编译现场。
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

std::string Compiler::makeTracePrefix() const {
    std::string prefix;
    prefix.reserve(static_cast<size_t>(traceIndent) * 4);
    for (uint32_t i = 0; i < traceIndent; ++i) {
        prefix += "|   ";
    }
    return prefix;
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
    // 作用域收束：
    // 对当前深度声明的局部变量逐个退栈，必要时发射 OP_FREE 并回收寄存器。
    scopeDepth--;
    while (locals.size() > 0 && locals.back().depth > scopeDepth) {
        Local &local = locals.back();
        if (local.is_owned && !local.is_moved) {
            emitOp(vm::OPCODE::OP_FREE, local.reg, currentPool->locations.back().line,
                   currentPool->locations.back().column);
        }

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
    // 仅仅返回没找到的标记（255），不要在这里报错！
    // 因为调用者可能需要退化为全局查找。
    return 255;
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
        reportError(0, 0, "Jump offset too large.");
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
    opcodeEmitCount[static_cast<size_t>(op)]++;
    emitByte(static_cast<uint8_t>(op), line, column);
}

void Compiler::emitOp(vm::OPCODE op, uint8_t operand_a, uint32_t line, uint32_t column) {
    opcodeEmitCount[static_cast<size_t>(op)]++;
    emitByte(static_cast<uint8_t>(op), line, column);
    emitByte(operand_a, line, column);
}

void Compiler::emitOp(vm::OPCODE op, uint8_t operand_a, uint8_t operand_b, uint32_t line, uint32_t column) {
    opcodeEmitCount[static_cast<size_t>(op)]++;
    emitByte(static_cast<uint8_t>(op), line, column);
    emitByte(operand_a, line, column);
    emitByte(operand_b, line, column);
}

void Compiler::emitOp(vm::OPCODE op, uint8_t operand_a, uint8_t operand_b, uint8_t operand_c, uint32_t line,
                      uint32_t column) {
    opcodeEmitCount[static_cast<size_t>(op)]++;
    emitByte(static_cast<uint8_t>(op), line, column);
    emitByte(operand_a, line, column);
    emitByte(operand_b, line, column);
    emitByte(operand_c, line, column);
}
void Compiler::emitOp(vm::OPCODE op, uint8_t operand_a, uint8_t operand_b, uint8_t operand_c, uint8_t operand_d,
                      uint32_t line, uint32_t column) {
    opcodeEmitCount[static_cast<size_t>(op)]++;
    emitByte(static_cast<uint8_t>(op), line, column);
    emitByte(operand_a, line, column);
    emitByte(operand_b, line, column);
    emitByte(operand_c, line, column);
    emitByte(operand_d, line, column);
}

void Compiler::emitConstant(vm::Value value, uint8_t targetReg, uint32_t line, uint32_t column) {
    uint16_t index = makeConstant(value, line, column);
    if (index < 255) {
        emitOp(vm::OPCODE::OP_LOAD_CONST, targetReg, static_cast<uint8_t>(index), line, column);
    } else {
        uint8_t high_byte = static_cast<uint8_t>((index >> 8) & 0xFF);
        uint8_t low_byte = static_cast<uint8_t>(index & 0xFF);
        emitOp(vm::OPCODE::OP_LOAD_CONST_W, targetReg, high_byte, low_byte, line, column);
    }
}

//---AST遍历核心(Visitor)---
void Compiler::compileNode(ASTNodeIndex nodeIdx) {
    // AST 节点分发器：
    // 表达式 -> compileExpression
    // 语句   -> compileStatement
    // 其余   -> compileDeclaration（顶层声明兜底）
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
    // ... 如果有专门的声明NodeType可在此补充
    default:
        // 兜底进入声明处理
        compileDeclaration(nodeIdx);
        break;
    }
}

//---错误与警告处�?--
void Compiler::reportError(const Token &token, std::string_view message) {
    reportError(token.line, token.column, message);
}

void Compiler::reportError(uint32_t line, uint32_t column, std::string_view message) {
    diagnostics.reportError(
        diagnostic::DiagnosticStage::Compiler, diagnostic::codes::compiler::GenericError, std::string(message),
        diagnostic::makeSourceSpan(currentPool != nullptr ? currentPool->source_path : "", line, column));
    hadError = true;
}

void Compiler::reportWarning(uint32_t line, uint32_t column, std::string_view message) { warningCount++; }

} // namespace niki::syntax
