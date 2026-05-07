#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include <limits>

/** @builder_core_impl: IRBuilder 核心管线与发射基元实现
 * 这个文件实现的是 Builder 的“中枢层”：构建入口、函数/块管理、寄存器分配和统一指令发射。
 * 声明、语句、表达式子模块最终都会回落到这里提供的上下文管理与 emit 基元。
 *
 * 从实现分层看，这里不是负责“理解语法节点语义”的地方，而是负责“把任何语义变成统一写入动作”的地方。
 * 例如 beginFunc/beginBlock 决定了线性表切片边界，emit/emitInstId 决定了 SoA 写入的一致格式，
 * ensureBlockTerminated 则保障每个块都满足后续 verify/lower 的结构预期。
 *
 * 因此该文件可以视作 IRBuilder 的“执行内核”：它把上层降解逻辑中的分支复杂度，收敛到稳定的数据写入协议。
 *
 * 字段流说明（核心）：
 * - `CompilationUnit` -> `BuildCtx.unit`：提供 AST、位置信息与字符串池快照来源。
 * - `BuildCtx.module`：承载本次构建所有产物；`beginFunc/beginBlock/emitInstId` 都写入这里。
 * - `FuncCtx.fid/cur_bid`：决定“当前发射写到哪个函数、哪个块”。
 * - `FuncCtx.emit_line/emit_col`：每条指令写入时同步记录源码映射。
 * - `FuncCtx.local_vreg_by_name_sid`：表达式/语句层通过它完成局部变量寄存器解析。
 * - `FuncCtx.loop_stack`：语句层借此完成 break/continue 的占位与回填。
 */
namespace niki::ir {
using namespace niki::syntax;

//------------------------------------------------------------------------------
// ENTRY: 构建入口，初始化上下文并驱动根节点降解。
//------------------------------------------------------------------------------
/**
 * @brief 执行单编译单元的 IR 构建主流程。
 * @param unit 编译单元（含 source_path、ASTPool、root、string pool）。
 * @return std::expected<ModuleIR, diagnostic::DiagnosticBag> 成功返回 ModuleIR，失败返回诊断集合。
 * @note 失败条件包括根节点降级失败或构建阶段出现诊断。
 */
std::expected<ModuleIR, diagnostic::DiagnosticBag> IRBuilder::build(CompilationUnit &unit,
                                                               const semantic::UnitVisibleSymbols *visible_symbols) {
    BuildCtx bc;
    bc.unit = &unit;
    bc.visible_symbols = visible_symbols;
    bc.module.module_id = unit.module_id;
    bc.module.module_name.clear();
    bc.module.module_src_path = unit.source_path;
    bc.module.string_pool = unit.pool.snapshotStringPool();

    if (!buildRoot(bc) || bc.diags.hasErrors()) {
        return std::unexpected(std::move(bc.diags));
    }

    return bc.module;
}

//------------------------------------------------------------------------------
// TABLE: 表访问与分配辅助，统一函数/块/寄存器的创建路径。
//------------------------------------------------------------------------------
/**
 * @brief 获取函数记录引用。
 * @param bc 构建上下文。
 * @param fid 函数 id。
 * @return FuncRecord& 对应函数记录。
 */
FuncRecord &IRBuilder::func(BuildCtx &bc, FuncId fid) { return bc.module.funcs[fid]; }

/**
 * @brief 获取函数内块记录引用。
 * @param bc 构建上下文。
 * @param fid 函数 id。
 * @param bid 函数内相对块 id。
 * @return BlockRecord& 对应块记录。
 */
BlockRecord &IRBuilder::block(BuildCtx &bc, FuncId fid, BlockId bid) {
    const FuncRecord &f = bc.module.funcs[fid];
    return bc.module.blocks[f.block_span.begin + bid];
}
/**
 * @brief 追加并初始化一个函数记录。
 * @param bc 构建上下文。
 * @param func_name_sid 函数字符串 id。
 * @return FuncId 新函数 id。
 */
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
/**
 * @brief 在当前函数下追加一个基本块。
 * @param bc 构建上下文。
 * @param fc 函数上下文。
 * @param debug_name 调试名称。
 * @return BlockId 新块 id（函数内相对编号）。
 */
BlockId IRBuilder::beginBlock(BuildCtx &bc, FuncCtx &fc, const char *debug_name) {
    FuncRecord &f = func(bc, fc.fid);
    BlockRecord b;
    b.block_id = f.block_span.count;
    b.debug_name_sid = bc.module.intern(debug_name ? debug_name : "");
    b.inst_span.begin = std::numeric_limits<uint32_t>::max();  // CHG-20260506 懒初始化：首条指令发射时回填。
    // 旧实现：b.inst_span.begin = bc.module.insts.size(); 在块创建时立即设 begin。
    // 问题：IfStmt/LoopStmt 在构建控制流图时先顺序创建多个块（then/else/join/body/exit），
    //       然后才递归 buildStmt 往各块中填充指令。后创建的块拿到的 begin 指向的是前面所有块的
    //       起始指令位置，导致 verifier 读到多块共用同一个 inst_span 起始偏移，误判为
    //       VerifyErrorCode::TerminatorNotLast 错误："块的终结指令不是块内最后一条指令"。
    b.inst_span.count = 0;
    bc.module.blocks.push_back(b);
    f.block_span.count += 1;

    return b.block_id;
}
/**
 * @brief 切换当前发射块。
 * @param fc 函数上下文。
 * @param bid 目标块 id。
 */
void IRBuilder::switchBlock(FuncCtx &fc, BlockId bid) { fc.cur_bid = bid; }

/**
 * @brief 更新当前发射位置信息（行列号）。
 * @param bc 构建上下文。
 * @param fc 函数上下文。
 * @param node_idx AST 节点索引。
 */
void IRBuilder::setEmitLocation(BuildCtx &bc, FuncCtx &fc, ASTNodeIndex node_idx) {
    if (bc.unit == nullptr || !node_idx.isvalid() || node_idx.index >= bc.unit->pool.locations.size()) {
        fc.emit_line = 0;
        fc.emit_col = 0;
        return;
    }
    fc.emit_line = bc.unit->pool.locations[node_idx.index].line;
    fc.emit_col = bc.unit->pool.locations[node_idx.index].column;
}
/**
 * @brief 分配一个新的虚拟寄存器。
 * @param bc 构建上下文。
 * @param fc 函数上下文。
 * @return RegId 新寄存器编号。
 */
RegId IRBuilder::allocVReg(BuildCtx &bc, FuncCtx &fc) {
    FuncRecord &f = func(bc, fc.fid);
    return f.next_vreg++;
}

//------------------------------------------------------------------------------
// EMIT_CORE: 原始指令发射路径，直接写入 SoA 指令表。
//------------------------------------------------------------------------------
/**
 * @brief 发射一条指令（不关心返回 id）。
 */
void IRBuilder::emit(BuildCtx &bc, FuncCtx &fc, InstKind k, ValueKind dk, uint32_t du32, int64_t di64, uint64_t du64,
                     ValueKind ak, uint32_t au32, int64_t ai64, uint64_t au64, ValueKind bk, uint32_t bu32,
                     int64_t bi64, uint64_t bu64, ValueKind ck, uint32_t cu32, int64_t ci64, uint64_t cu64,
                     uint32_t aux) {
    (void)emitInstId(bc, fc, k, dk, du32, di64, du64, ak, au32, ai64, au64, bk, bu32, bi64, bu64, ck, cu32, ci64, cu64,
                     aux);
}

/**
 * @brief 向指令表发射一条完整 IR 指令并返回其 id。
 * @param bc 构建上下文。
 * @param fc 函数上下文。
 * @param k 指令种类。
 * @return uint32_t 新增指令 id。
 * @note 该函数无显式失败路径，调用方需保证上下文有效。
 */
uint32_t IRBuilder::emitInstId(BuildCtx &bc, FuncCtx &fc, InstKind k, ValueKind dk, uint32_t du32, int64_t di64,
                               uint64_t du64, ValueKind ak, uint32_t au32, int64_t ai64, uint64_t au64, ValueKind bk,
                               uint32_t bu32, int64_t bi64, uint64_t bu64, ValueKind ck, uint32_t cu32, int64_t ci64,
                               uint64_t cu64, uint32_t aux) {
    // CHG-20260506 懒初始化：块的首条指令发射时回填 inst_span.begin。
    // 与 beginBlock 中设 UINT32_MAX 配对，解决多块背靠背创建时的 inst_span 重叠问题。
    BlockRecord &cur = block(bc, fc.fid, fc.cur_bid);
    if (cur.inst_span.begin == std::numeric_limits<uint32_t>::max()) {
        cur.inst_span.begin = bc.module.insts.size();
    }
    const uint32_t inst_id = bc.module.insts.push(k, dk, du32, di64, du64, ak, au32, ai64, au64, bk, bu32, bi64, bu64,
                                                  ck, cu32, ci64, cu64, aux, fc.emit_line, fc.emit_col);
    cur.inst_span.count += 1;

    return inst_id;
}

//------------------------------------------------------------------------------
// EMIT_HELPER: 薄封装发射函数，减少调用点样板代码。
//------------------------------------------------------------------------------
/**
 * @brief 发射寄存器到寄存器的 Move 指令。
 */
void IRBuilder::emitMoveRegToReg(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, RegId src_reg) {
    emit(build_ctx, func_ctx, InstKind::Move, ValueKind::VReg, dst_reg, 0, 0, ValueKind::VReg, src_reg, 0, 0,
         ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

/**
 * @brief 发射一元寄存器指令。
 */
void IRBuilder::emitUnaryReg(BuildCtx &build_ctx, FuncCtx &func_ctx, InstKind inst_kind, RegId dst_reg,
                             RegId operand_reg) {
    emit(build_ctx, func_ctx, inst_kind, ValueKind::VReg, dst_reg, 0, 0, ValueKind::VReg, operand_reg, 0, 0,
         ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

/**
 * @brief 发射二元寄存器指令。
 */
void IRBuilder::emitBinaryReg(BuildCtx &build_ctx, FuncCtx &func_ctx, InstKind inst_kind, RegId dst_reg, RegId left_reg,
                              RegId right_reg) {
    emit(build_ctx, func_ctx, inst_kind, ValueKind::VReg, dst_reg, 0, 0, ValueKind::VReg, left_reg, 0, 0,
         ValueKind::VReg, right_reg, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

/**
 * @brief 发射无条件跳转到目标块。
 */
void IRBuilder::emitJumpToBlock(BuildCtx &build_ctx, FuncCtx &func_ctx, BlockId target_block_id) {
    emit(build_ctx, func_ctx, InstKind::Jump, ValueKind::Invalid, 0, 0, 0, ValueKind::BlockId, target_block_id, 0, 0,
         ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

/**
 * @brief 发射占位跳转并返回指令 id（用于后续回填）。
 * @return uint32_t 占位跳转指令 id。
 */
uint32_t IRBuilder::emitJumpPlaceholder(BuildCtx &build_ctx, FuncCtx &func_ctx, uint32_t placeholder_block_id) {
    return emitInstId(build_ctx, func_ctx, InstKind::Jump, ValueKind::Invalid, 0, 0, 0, ValueKind::BlockId,
                      placeholder_block_id, 0, 0, ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

/**
 * @brief 发射条件分支指令。
 */
void IRBuilder::emitBranchOnReg(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId condition_reg, BlockId true_block_id,
                                BlockId false_block_id) {
    emit(build_ctx, func_ctx, InstKind::Branch, ValueKind::Invalid, 0, 0, 0, ValueKind::VReg, condition_reg, 0, 0,
         ValueKind::BlockId, true_block_id, 0, 0, ValueKind::BlockId, false_block_id, 0, 0);
}

/**
 * @brief 发射 i64 常量加载指令。
 */
void IRBuilder::emitConstantI64(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, int64_t value) {
    emit(build_ctx, func_ctx, InstKind::Constant, ValueKind::VReg, dst_reg, 0, 0, ValueKind::ImmI64, 0, value, 0,
         ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

/**
 * @brief 发射 bool 常量加载指令。
 */
void IRBuilder::emitConstantBool(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, bool value) {
    emit(build_ctx, func_ctx, InstKind::Constant, ValueKind::VReg, dst_reg, 0, 0, ValueKind::ImmBool, value ? 1u : 0u,
         0, 0, ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

/**
 * @brief 发射 f64 bits 常量加载指令。
 */
void IRBuilder::emitConstantF64Bits(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, uint64_t value_bits) {
    emit(build_ctx, func_ctx, InstKind::Constant, ValueKind::VReg, dst_reg, 0, 0, ValueKind::ImmF64Bits, 0, 0,
         value_bits, ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

/**
 * @brief 发射字符串 id 常量加载指令。
 */
void IRBuilder::emitConstantStringId(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, uint32_t string_id) {
    emit(build_ctx, func_ctx, InstKind::Constant, ValueKind::VReg, dst_reg, 0, 0, ValueKind::StringId, string_id, 0, 0,
         ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
}

/**
 * @brief 发射释放堆对象的 Free 指令（dst 为待清空的目标虚拟寄存器）。
 */
void IRBuilder::emitFree(BuildCtx &bc, FuncCtx &fc, RegId vreg) {
    emit(bc, fc, InstKind::Free, ValueKind::VReg, vreg, 0, 0, ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0,
         ValueKind::Invalid, 0, 0, 0);
}

void IRBuilder::emitBlockExitFreesFromPool(BuildCtx &bc, FuncCtx &fc, ASTNodeIndex block_stmt_idx) {
    if (!block_stmt_idx.isvalid() || bc.unit == nullptr) {
        return;
    }
    auto it = bc.unit->pool.block_exit_free_name_ids.find(block_stmt_idx.index);
    if (it == bc.unit->pool.block_exit_free_name_ids.end()) {
        return;
    }
    for (uint32_t name_sid : it->second) {
        auto reg_it = fc.local_vreg_by_name_sid.find(name_sid);
        if (reg_it != fc.local_vreg_by_name_sid.end()) {
            emitFree(bc, fc, reg_it->second);
        }
    }
}

void IRBuilder::emitFuncExitFreesFromPool(BuildCtx &bc, FuncCtx &fc, ASTNodeIndex func_decl_idx) {
    if (!func_decl_idx.isvalid() || bc.unit == nullptr) {
        return;
    }
    auto it = bc.unit->pool.func_exit_free_name_ids.find(func_decl_idx.index);
    if (it == bc.unit->pool.func_exit_free_name_ids.end()) {
        return;
    }
    for (uint32_t name_sid : it->second) {
        auto reg_it = fc.local_vreg_by_name_sid.find(name_sid);
        if (reg_it != fc.local_vreg_by_name_sid.end()) {
            emitFree(bc, fc, reg_it->second);
        }
    }
}

void IRBuilder::emitAllOpenScopeFreesBeforeReturn(BuildCtx &bc, FuncCtx &fc) {
    for (auto it = fc.block_stack.rbegin(); it != fc.block_stack.rend(); ++it) {
        emitBlockExitFreesFromPool(bc, fc, *it);
    }
    if (fc.func_decl_node_idx.isvalid()) {
        emitFuncExitFreesFromPool(bc, fc, fc.func_decl_node_idx);
    }
}

//------------------------------------------------------------------------------
// TERMINATOR: 基本块终结与诊断辅助。
//------------------------------------------------------------------------------
/**
 * @brief 发射无返回值的 Return（Invalid）。
 */
void IRBuilder::emitReturnInvalid(BuildCtx &bc, FuncCtx &fc) {
    emit(bc, fc, InstKind::Return, ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0,
         0, ValueKind::Invalid, 0, 0, 0, 0);
}

/**
 * @brief 判断当前块是否已由终结符结束。
 * @return true 当前块尾为 Jump/Branch/Return。
 * @return false 当前块为空或尾指令非终结符。
 */
bool IRBuilder::isCurrentBlockTerminated(BuildCtx &bc, FuncCtx &fc) {
    const BlockRecord &b = block(bc, fc.fid, fc.cur_bid);
    if (b.inst_span.count == 0) {
        return false;
    }
    const uint32_t last_inst = b.inst_span.begin + b.inst_span.count - 1;
    const InstKind kind = bc.module.insts.kind[last_inst];
    return kind == InstKind::Jump || kind == InstKind::Branch || kind == InstKind::Return;
}

/**
 * @brief 确保当前块以终结符结束。
 * @return true 始终返回 true；必要时会补发默认 Return。
 */
bool IRBuilder::ensureBlockTerminated(BuildCtx &bc, FuncCtx &fc) {
    if (!isCurrentBlockTerminated(bc, fc)) {
        emitReturnInvalid(bc, fc);
    }
    return true;
}

/**
 * @brief 记录一条 IR 构建错误到诊断集合。
 * @param bc 构建上下文。
 * @param msg 错误消息文本。
 * @param idx AST 节点索引，用于定位源码位置。
 */
void IRBuilder::error(BuildCtx &bc, const std::string &msg, ASTNodeIndex idx) {
    uint32_t line = 0, col = 0;
    if (bc.unit && idx.isvalid() && idx.index < bc.unit->pool.locations.size()) {
        line = bc.unit->pool.locations[idx.index].line;
        col = bc.unit->pool.locations[idx.index].column;
    }
    bc.diags.error(diagnostic::events::IRCode::GenericError, msg,
                   diagnostic::makeSourceSpan(bc.unit ? bc.unit->source_path : "", line, col));
}

} // namespace niki::ir