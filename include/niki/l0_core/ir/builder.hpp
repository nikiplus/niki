#pragma once
#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/semantic/global_compilation.hpp"
#include <expected>
#include <unordered_map>
#include <vector>
namespace niki::ir {
class IRBuilder {
  public:
    std::expected<ModuleIR, diagnostic::DiagnosticBag> build(GlobalCompilationUnit &unit);

  private:
    struct LoopPatch {
        BlockId continue_target = std::numeric_limits<BlockId>::max();
        std::vector<uint32_t> break_jump_inst_ids;
    };
    struct BuildCtx {
        GlobalCompilationUnit *unit = nullptr;
        ModuleIR module;
        diagnostic::DiagnosticBag diags;
    };
    struct FuncCtx {
        FuncId fid = std::numeric_limits<FuncId>::max();
        BlockId cur_bid = std::numeric_limits<BlockId>::max();
        std::unordered_map<uint32_t, RegId> local_vreg_by_name_sid;
        std::vector<LoopPatch> loop_stack;
    };
    // TOP_LEVEL: 顶层声明降解入口。
    bool buildRoot(BuildCtx &bc);
    bool buildTopDecl(BuildCtx &bc, niki::syntax::ASTNodeIndex decl_idx);
    bool buildFuncDecl(BuildCtx &bc, niki::syntax::ASTNodeIndex decl_idx);
    // STMT_EXPR: 语句与表达式降解入口。
    bool buildStmt(BuildCtx &bc, FuncCtx &fc, niki::syntax::ASTNodeIndex stmt_idx);
    bool buildExpr(BuildCtx &bc, FuncCtx &fc, niki::syntax::ASTNodeIndex expr_idx, RegId *out);
    // EMIT_HELPER: 指令发射与块管理辅助。
    FuncRecord &func(BuildCtx &bc, FuncId fid);
    BlockRecord &block(BuildCtx &bc, FuncId fid, BlockId bid);
    FuncId beginFunc(BuildCtx &bc, uint32_t func_name_sid);
    BlockId beginBlock(BuildCtx &bc, FuncCtx &fc, const char *debug_name);
    void switchBlock(FuncCtx &fc, BlockId bid);
    RegId allocVReg(BuildCtx &bc, FuncCtx &fc);
    void emit(BuildCtx &bc, FuncCtx &fc, InstKind k, ValueKind dk, uint32_t du32, int64_t di64, uint64_t du64,
              ValueKind ak, uint32_t au32, int64_t ai64, uint64_t au64, ValueKind bk, uint32_t bu32, int64_t bi64,
              uint64_t bu64, ValueKind ck, uint32_t cu32, int64_t ci64, uint64_t cu64, uint32_t aux = 0);
    uint32_t emitInstId(BuildCtx &bc, FuncCtx &fc, InstKind k, ValueKind dk, uint32_t du32, int64_t di64, uint64_t du64,
                        ValueKind ak, uint32_t au32, int64_t ai64, uint64_t au64, ValueKind bk, uint32_t bu32,
                        int64_t bi64, uint64_t bu64, ValueKind ck, uint32_t cu32, int64_t ci64, uint64_t cu64,
                        uint32_t aux = 0);
    void emitMoveRegToReg(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, RegId src_reg);
    void emitUnaryReg(BuildCtx &build_ctx, FuncCtx &func_ctx, InstKind inst_kind, RegId dst_reg, RegId operand_reg);
    void emitBinaryReg(BuildCtx &build_ctx, FuncCtx &func_ctx, InstKind inst_kind, RegId dst_reg, RegId left_reg,
                       RegId right_reg);
    void emitJumpToBlock(BuildCtx &build_ctx, FuncCtx &func_ctx, BlockId target_block_id);
    uint32_t emitJumpPlaceholder(BuildCtx &build_ctx, FuncCtx &func_ctx, uint32_t placeholder_block_id);
    void emitBranchOnReg(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId condition_reg, BlockId true_block_id,
                         BlockId false_block_id);
    void emitConstantI64(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, int64_t value);
    void emitConstantBool(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, bool value);
    void emitConstantF64Bits(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, uint64_t value_bits);
    void emitConstantStringId(BuildCtx &build_ctx, FuncCtx &func_ctx, RegId dst_reg, uint32_t string_id);
    void emitReturnInvalid(BuildCtx &bc, FuncCtx &fc);
    bool isCurrentBlockTerminated(BuildCtx &bc, FuncCtx &fc);
    bool ensureBlockTerminated(BuildCtx &bc, FuncCtx &fc);
    void error(BuildCtx &bc, const std::string &msg, niki::syntax::ASTNodeIndex idx);
};
} // namespace niki::ir