// FILE: IRBuilder 主流程与发射辅助实现。
#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/syntax/ast.hpp"

namespace niki::ir {
using namespace niki::syntax;

//------------------------------------------------------------------------------
// ENTRY: 构建入口，初始化上下文并驱动根节点降解。
//------------------------------------------------------------------------------
std::expected<ModuleIR, diagnostic::DiagnosticBag> IRBuilder::build(GlobalCompilationUnit &unit) {
    BuildCtx bc;
    bc.unit = &unit;
    bc.module.module_name = unit.source_path;
    bc.module.module_src_path = unit.source_path;
    bc.module.string_pool = unit.pool.snapshotStringPool();
    if (!buildRoot(bc) || !bc.diags.empty()) {
        return std::unexpected(std::move(bc.diags));
    }
    bc.module.string_pool = unit.pool.snapshotStringPool();
    return bc.module;
}

//------------------------------------------------------------------------------
// ROOT: 顶层节点降解，负责模块根与声明列表分发。
//------------------------------------------------------------------------------
bool IRBuilder::buildRoot(BuildCtx &bc) {
    if (bc.unit == nullptr || !bc.unit->root.isvalid()) {
        bc.diags.error(diagnostic::events::CompilerCode::InvalidRoot, "invalid module root",
                       diagnostic::makeSourceSpan(bc.unit ? bc.unit->source_path : ""));
        return false;
    }
    const ASTNode &root = bc.unit->pool.getNode(bc.unit->root);
    if (root.type != NodeType::ModuleDecl && root.type != NodeType::ProgramRoot) {
        bc.diags.error(diagnostic::events::CompilerCode::InvalidRoot, "IR builder expects ModuleDecl or ProgramRoot root.",
                       diagnostic::makeSourceSpan(bc.unit->source_path));
        return false;
    }
    ASTNodeIndex body_idx = (root.type == NodeType::ModuleDecl) ? root.payload.module_decl.body : bc.unit->root;
    const ASTNode &body = bc.unit->pool.getNode(body_idx);
    auto decls = bc.unit->pool.get_list(body.payload.list.elements);
    bool ok = true;
    for (ASTNodeIndex d : decls)
        ok = buildTopDecl(bc, d) && ok;
    return ok;
}

//------------------------------------------------------------------------------
// TABLE: 表访问与分配辅助，统一函数/块/寄存器的创建路径。
//------------------------------------------------------------------------------
FuncRecord &IRBuilder::func(BuildCtx &bc, FuncId fid) { return bc.module.funcs[fid]; }
BlockRecord &IRBuilder::block(BuildCtx &bc, FuncId fid, BlockId bid) {
    const FuncRecord &f = bc.module.funcs[fid];
    return bc.module.blocks[f.block_span.begin + bid];
}
FuncId IRBuilder::beginFunc(BuildCtx &bc, uint32_t func_name_sid) {
    FuncRecord f;
    f.func_id = static_cast<FuncId>(bc.module.funcs.size());
    f.func_name_sid = func_name_sid;
    f.src_sid = bc.module.intern(bc.unit ? bc.unit->source_path : "");
    f.block_span.begin = static_cast<uint32_t>(bc.module.blocks.size());
    f.block_span.count = 0;
    bc.module.funcs.push_back(f);
    return f.func_id;
}
BlockId IRBuilder::beginBlock(BuildCtx &bc, FuncCtx &fc, const char *debug_name) {
    FuncRecord &f = func(bc, fc.fid);
    BlockRecord b;
    b.block_id = f.block_span.count;
    b.debug_name_sid = bc.module.intern(debug_name ? debug_name : "");
    b.inst_span.begin = bc.module.insts.size();
    b.inst_span.count = 0;
    bc.module.blocks.push_back(b);
    f.block_span.count += 1;
    return b.block_id;
}
void IRBuilder::switchBlock(FuncCtx &fc, BlockId bid) { fc.cur_bid = bid; }
RegId IRBuilder::allocVReg(BuildCtx &bc, FuncCtx &fc) {
    FuncRecord &f = func(bc, fc.fid);
    return f.next_vreg++;
}

//------------------------------------------------------------------------------
// EMIT_CORE: 原始指令发射路径，直接写入 SoA 指令表。
//------------------------------------------------------------------------------
void IRBuilder::emit(BuildCtx &bc, FuncCtx &fc, InstKind k, ValueKind dk, uint32_t du32, int64_t di64, uint64_t du64,
                     ValueKind ak, uint32_t au32, int64_t ai64, uint64_t au64, ValueKind bk, uint32_t bu32,
                     int64_t bi64, uint64_t bu64, ValueKind ck, uint32_t cu32, int64_t ci64, uint64_t cu64,
                     uint32_t aux) {
    (void)emitInstId(bc, fc, k, dk, du32, di64, du64, ak, au32, ai64, au64, bk, bu32, bi64, bu64, ck, cu32, ci64, cu64,
                     aux);
}

uint32_t IRBuilder::emitInstId(BuildCtx &bc, FuncCtx &fc, InstKind k, ValueKind dk, uint32_t du32, int64_t di64,
                               uint64_t du64, ValueKind ak, uint32_t au32, int64_t ai64, uint64_t au64, ValueKind bk,
                               uint32_t bu32, int64_t bi64, uint64_t bu64, ValueKind ck, uint32_t cu32, int64_t ci64,
                               uint64_t cu64, uint32_t aux) {
    const uint32_t inst_id = bc.module.insts.push(k, dk, du32, di64, du64, ak, au32, ai64, au64, bk, bu32, bi64, bu64,
                                                  ck, cu32, ci64, cu64, aux, 0, 0);
    block(bc, fc.fid, fc.cur_bid).inst_span.count += 1;
    return inst_id;
}

//------------------------------------------------------------------------------
// EMIT_HELPER: 薄封装发射函数，减少调用点样板代码。
//------------------------------------------------------------------------------
void IRBuilder::emitMoveRegToReg(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, RegId src_reg) {
    emit(build_ctx, func_ctx, InstKind::Move, ValueKind::VReg, dst_reg, 0, 0, ValueKind::VReg, src_reg, 0, 0,
         ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

void IRBuilder::emitUnaryReg(BuildCtx &build_ctx, FuncCtx &func_ctx, InstKind inst_kind, RegId dst_reg, RegId operand_reg) {
    emit(build_ctx, func_ctx, inst_kind, ValueKind::VReg, dst_reg, 0, 0, ValueKind::VReg, operand_reg, 0, 0,
         ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

void IRBuilder::emitBinaryReg(BuildCtx &build_ctx, FuncCtx &func_ctx, InstKind inst_kind, RegId dst_reg, RegId left_reg,
                              RegId right_reg) {
    emit(build_ctx, func_ctx, inst_kind, ValueKind::VReg, dst_reg, 0, 0, ValueKind::VReg, left_reg, 0, 0, ValueKind::VReg,
         right_reg, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

void IRBuilder::emitJumpToBlock(BuildCtx &build_ctx, FuncCtx &func_ctx, BlockId target_block_id) {
    emit(build_ctx, func_ctx, InstKind::Jump, ValueKind::Invalid, 0, 0, 0, ValueKind::BlockId, target_block_id, 0, 0,
         ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

uint32_t IRBuilder::emitJumpPlaceholder(BuildCtx &build_ctx, FuncCtx &func_ctx, uint32_t placeholder_block_id) {
    return emitInstId(build_ctx, func_ctx, InstKind::Jump, ValueKind::Invalid, 0, 0, 0, ValueKind::BlockId,
                      placeholder_block_id, 0, 0, ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

void IRBuilder::emitBranchOnReg(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId condition_reg, BlockId true_block_id,
                                BlockId false_block_id) {
    emit(build_ctx, func_ctx, InstKind::Branch, ValueKind::Invalid, 0, 0, 0, ValueKind::VReg, condition_reg, 0, 0,
         ValueKind::BlockId, true_block_id, 0, 0, ValueKind::BlockId, false_block_id, 0, 0);
}

void IRBuilder::emitConstantI64(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, int64_t value) {
    emit(build_ctx, func_ctx, InstKind::Constant, ValueKind::VReg, dst_reg, 0, 0, ValueKind::ImmI64, 0, value, 0,
         ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

void IRBuilder::emitConstantBool(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, bool value) {
    emit(build_ctx, func_ctx, InstKind::Constant, ValueKind::VReg, dst_reg, 0, 0, ValueKind::ImmBool, value ? 1u : 0u, 0, 0,
         ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

void IRBuilder::emitConstantF64Bits(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, uint64_t value_bits) {
    emit(build_ctx, func_ctx, InstKind::Constant, ValueKind::VReg, dst_reg, 0, 0, ValueKind::ImmF64Bits, 0, 0, value_bits,
         ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

void IRBuilder::emitConstantStringId(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, uint32_t string_id) {
    emit(build_ctx, func_ctx, InstKind::Constant, ValueKind::VReg, dst_reg, 0, 0, ValueKind::StringId, string_id, 0, 0,
         ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

//------------------------------------------------------------------------------
// TERMINATOR: 基本块终结与诊断辅助。
//------------------------------------------------------------------------------
void IRBuilder::emitReturnInvalid(BuildCtx &bc, FuncCtx &fc) {
    emit(bc, fc, InstKind::Return, ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0,
         0, ValueKind::Invalid, 0, 0, 0, 0);
}

bool IRBuilder::isCurrentBlockTerminated(BuildCtx &bc, FuncCtx &fc) {
    const BlockRecord &b = block(bc, fc.fid, fc.cur_bid);
    if (b.inst_span.count == 0) {
        return false;
    }
    const uint32_t last_inst = b.inst_span.begin + b.inst_span.count - 1;
    const InstKind kind = bc.module.insts.kind[last_inst];
    return kind == InstKind::Jump || kind == InstKind::Branch || kind == InstKind::Return;
}

bool IRBuilder::ensureBlockTerminated(BuildCtx &bc, FuncCtx &fc) {
    if (!isCurrentBlockTerminated(bc, fc)) {
        emitReturnInvalid(bc, fc);
    }
    return true;
}

void IRBuilder::error(BuildCtx &bc, const std::string &msg, ASTNodeIndex idx) {
    uint32_t line = 0, col = 0;
    if (bc.unit && idx.isvalid() && idx.index < bc.unit->pool.locations.size()) {
        line = bc.unit->pool.locations[idx.index].line;
        col = bc.unit->pool.locations[idx.index].column;
    }
    bc.diags.error(diagnostic::events::CompilerCode::GenericError, msg,
                   diagnostic::makeSourceSpan(bc.unit ? bc.unit->source_path : "", line, col));
}

} // namespace niki::ir