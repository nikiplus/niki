#include "niki/l0_core/ir/builder.hpp"

#include "niki/l0_core/syntax/ast.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace niki::ir {
using namespace niki::syntax;

namespace {
IRInst makeSimpleInst(IRInstKind kind, IRValue dst = IRValue::makeInvalid(), IRValue a = IRValue::makeInvalid(),
                      IRValue b = IRValue::makeInvalid(), IRValue c = IRValue::makeInvalid(), uint32_t aux = 0,
                      uint32_t line = 0, uint32_t column = 0) {
    IRInst inst;
    inst.instruction_kind = kind;
    inst.destination_value = dst;
    inst.first_operand = a;
    inst.second_operand = b;
    inst.third_operand = c;
    inst.auxiliary_data = aux;
    inst.source_line = line;
    inst.source_column = column;
    return inst;
}
} // namespace

std::expected<ModuleIR, diagnostic::DiagnosticBag> IRBuilder::build(GlobalCompilationUnit &unit) {
    return build(unit, nullptr, nullptr);
}

std::expected<ModuleIR, diagnostic::DiagnosticBag> IRBuilder::build(GlobalCompilationUnit &unit,
                                                                    const GlobalSymbolTable *global_symbols,
                                                                    const GlobalTypeArena *global_arena) {
    unit_ = &unit;
    global_syms_ = global_symbols;
    global_arena_ = global_arena;
    diags_ = diagnostic::DiagnosticBag{};
    module_ir_ = ModuleIR{};

    module_ir_.module_name = unit.source_path;
    module_ir_.module_src_path = unit.source_path;
    module_ir_.module_string_pool = unit.pool.snapshotStringPool();

    if (!buildModuleRoot()) {
        return std::unexpected(std::move(diags_));
    }
    if (!diags_.empty()) {
        return std::unexpected(std::move(diags_));
    }

    // 构建过程中可能因字符串字面量/成员名再次触发 intern，这里刷新快照确保 StringId 可验证。
    module_ir_.module_string_pool = unit.pool.snapshotStringPool();
    return module_ir_;
}

bool IRBuilder::buildModuleRoot() {
    if (unit_ == nullptr || !unit_->root.isvalid()) {
        diags_.error(diagnostic::events::CompilerCode::InvalidRoot, "Invalid module root for IR builder.",
                     diagnostic::makeSourceSpan(unit_ != nullptr ? unit_->source_path : ""));
        return false;
    }

    const ASTNode &root = unit_->pool.getNode(unit_->root);
    if (root.type != NodeType::ModuleDecl && root.type != NodeType::ProgramRoot) {
        diags_.error(diagnostic::events::CompilerCode::InvalidRoot, "IR builder expects ModuleDecl or ProgramRoot root.",
                     diagnostic::makeSourceSpan(unit_->source_path));
        return false;
    }

    ASTNodeIndex body_idx = ASTNodeIndex::invalid();
    if (root.type == NodeType::ModuleDecl) {
        body_idx = root.payload.module_decl.body;
    } else {
        body_idx = unit_->root;
    }

    const ASTNode &body = unit_->pool.getNode(body_idx);
    ASTListIndex list_index = body.payload.list.elements;
    auto elements = unit_->pool.get_list(list_index);

    bool ok = true;
    for (ASTNodeIndex decl_idx : elements) {
        ok = buildTopLvDecl(decl_idx) && ok;
    }
    return ok;
}

void IRBuilder::reportError(const std::string &message, ASTNodeIndex node_idx) {
    uint32_t line = 0;
    uint32_t column = 0;
    if (unit_ != nullptr && node_idx.isvalid() && node_idx.index < unit_->pool.locations.size()) {
        line = unit_->pool.locations[node_idx.index].line;
        column = unit_->pool.locations[node_idx.index].column;
    }

    diags_.error(diagnostic::events::CompilerCode::GenericError, message,
                 diagnostic::makeSourceSpan(unit_ != nullptr ? unit_->source_path : "", line, column));
}

IRBasicBlock &IRBuilder::appendBlock(FuncBuildCtx &ctx, const std::string &debug_name) {
    IRBlockId previous_block_id = std::numeric_limits<IRBlockId>::max();
    if (ctx.current_block != nullptr) {
        previous_block_id = ctx.current_block->block_id;
    }

    IRBasicBlock &new_block = ctx.func->createBasicBlock(debug_name);

    if (previous_block_id != std::numeric_limits<IRBlockId>::max()) {
        ctx.current_block = findBlockById(*ctx.func, previous_block_id);
    }

    IRBasicBlock *resolved_new_block = findBlockById(*ctx.func, new_block.block_id);
    return resolved_new_block != nullptr ? *resolved_new_block : new_block;
}

void IRBuilder::switchToBlock(FuncBuildCtx &ctx, IRBasicBlock &block) { ctx.current_block = &block; }

void IRBuilder::emitInst(FuncBuildCtx &ctx, const IRInst &inst) {
    if (ctx.current_block == nullptr) {
        return;
    }
    ctx.current_block->instruction_list.push_back(inst);
}

void IRBuilder::emitJump(FuncBuildCtx &ctx, IRBlockId target_block_id) {
    emitInst(ctx, makeSimpleInst(IRInstKind::Jump, IRValue::makeInvalid(),
                                 IRValue::makeBlockIdentifierValue(target_block_id)));
}

void IRBuilder::emitBranch(FuncBuildCtx &ctx, IRRegId cond_reg, IRBlockId true_block_id, IRBlockId false_block_id) {
    emitInst(ctx,
             makeSimpleInst(IRInstKind::Branch, IRValue::makeInvalid(), IRValue::makeVirtualRegisterValue(cond_reg),
                            IRValue::makeBlockIdentifierValue(true_block_id),
                            IRValue::makeBlockIdentifierValue(false_block_id)));
}

void IRBuilder::emitReturn(FuncBuildCtx &ctx, const IRValue &ret_val) {
    emitInst(ctx, makeSimpleInst(IRInstKind::Return, IRValue::makeInvalid(), ret_val));
}

bool IRBuilder::ensureBlockTerminated(FuncBuildCtx &ctx) {
    if (ctx.current_block == nullptr) {
        return true;
    }
    if (isCurrentBlockTerminated(ctx)) {
        return true;
    }
    emitReturn(ctx, IRValue::makeInvalid());
    return true;
}

bool IRBuilder::isCurrentBlockTerminated(const FuncBuildCtx &ctx) const {
    if (ctx.current_block == nullptr || ctx.current_block->instruction_list.empty()) {
        return false;
    }
    IRInstKind last_kind = ctx.current_block->instruction_list.back().instruction_kind;
    return last_kind == IRInstKind::Jump || last_kind == IRInstKind::Branch || last_kind == IRInstKind::Return;
}

IRSymbolId IRBuilder::ensureSymbol(uint32_t name_id, IRSymbolKind sym_kind, const IRType &sym_type, bool is_exported) {
    return module_ir_.addSym(name_id, sym_kind, sym_type, unit_ != nullptr ? unit_->source_path : "", is_exported);
}

} // namespace niki::ir
