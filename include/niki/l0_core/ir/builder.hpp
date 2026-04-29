#pragma once
#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/semantic/global_compilation.hpp"
#include <expected>
#include <unordered_map>
#include <vector>

/** @builder: AST/语义到 IR 的结构化降级器
 * 这个模块的本质不是“翻译字符串”，而是“把树形程序语义变成可执行图结构”。
 * Parser 输出的是 AST（树），TypeChecker 补的是类型语义；而执行器最终需要的是线性指令流 + 可跳转控制流。
 * Builder 正是这两种世界之间的转换层。
 *
 * 从算法角度，Builder 做了三件关键事情：
 * 1) 结构遍历：从模块根开始，按声明/语句/表达式递归下降；
 * 2) 状态建模：维护函数上下文（当前块、当前位置、局部符号到寄存器映射）；
 * 3) 指令发射：把高层语义归一到固定槽位格式（dst/a/b/c + aux）。
 *
 * 为什么这层必须独立存在？因为“树求值顺序”和“控制流图执行顺序”并不等价。
 * 例如 if/loop 在 AST 中是节点嵌套关系，在执行层则必须拆成 then/else/join、cond/body/exit 等显式块。
 * break/continue 甚至需要先发占位跳转，再在知道出口块后回填，这是一种典型的一次扫描 + 二次修正策略。
 *
 * `BuildCtx` 和 `FuncCtx` 的分层也是有意设计：
 * - BuildCtx 管模块级资源（module 容器、诊断）；
 * - FuncCtx 管函数级瞬态（cur_bid、next vreg、loop patch 栈）。
 * 这样可避免跨函数污染状态，并使函数降级在逻辑上可重入、可测试。
 *
 * 从底层约束看，后端寄存器编码通常有位宽限制，Builder 必须在源头统一寄存器分配与引用方式，
 * 否则 lower 阶段只能被动报错。换句话说，Builder 不只是“生成 IR”，它同时在构造“可编码的 IR”。
 *
 * 最后，Builder 也是诊断边界：遇到未支持节点、非法左值、结构不闭合等问题时应在此阶段前置失败，
 * 避免错误延迟到运行期才暴露，提升编译链路可解释性。
 */
namespace niki::ir {
class IRBuilder {
  public:
    //---模块级构建入口---
    // 输入：单编译单元（AST + 语义上下文）。
    // 输出：可验证的 ModuleIR 或诊断包。
    std::expected<ModuleIR, diagnostic::DiagnosticBag> build(GlobalCompilationUnit &unit);

  private:
    //---循环回填信息（函数内临时状态）---
    struct LoopPatch {
        // continue 目标块（通常为 loop.cond）。
        BlockId continue_target = std::numeric_limits<BlockId>::max();
        // 所有 break 占位 jump 指令 id，函数体降级结束后统一回填。
        std::vector<uint32_t> break_jump_inst_ids;
    };
    //---模块级构建上下文---
    struct BuildCtx {
        // 当前正在降级的编译单元。
        GlobalCompilationUnit *unit = nullptr;
        // 正在构建的模块 IR 产物。
        ModuleIR module;
        // 构建阶段诊断集合。
        diagnostic::DiagnosticBag diags;
    };
    //---函数级构建上下文---
    struct FuncCtx {
        // 当前函数 id。
        FuncId fid = std::numeric_limits<FuncId>::max();
        // 当前发射目标块 id。
        BlockId cur_bid = std::numeric_limits<BlockId>::max();
        // 当前发射源码位置（用于指令级调试映射）。
        uint32_t emit_line = 0;
        uint32_t emit_col = 0;
        // 局部变量名 sid -> 虚拟寄存器映射。
        std::unordered_map<uint32_t, RegId> local_vreg_by_name_sid;
        // 循环栈（支持嵌套 loop 的 break/continue 回填）。
        std::vector<LoopPatch> loop_stack;
    };
    //---顶层声明降解入口---
    bool buildRoot(BuildCtx &bc);
    bool buildTopDecl(BuildCtx &bc, niki::syntax::ASTNodeIndex decl_idx);
    bool buildFuncDecl(BuildCtx &bc, niki::syntax::ASTNodeIndex decl_idx);
    //---语句/表达式降解入口---
    bool buildStmt(BuildCtx &bc, FuncCtx &fc, niki::syntax::ASTNodeIndex stmt_idx);
    bool buildExpr(BuildCtx &bc, FuncCtx &fc, niki::syntax::ASTNodeIndex expr_idx, RegId *out);
    //---函数/块/寄存器管理辅助---
    FuncRecord &func(BuildCtx &bc, FuncId fid);
    BlockRecord &block(BuildCtx &bc, FuncId fid, BlockId bid);
    FuncId beginFunc(BuildCtx &bc, uint32_t func_name_sid);
    BlockId beginBlock(BuildCtx &bc, FuncCtx &fc, const char *debug_name);
    void switchBlock(FuncCtx &fc, BlockId bid);
    void setEmitLocation(BuildCtx &bc, FuncCtx &fc, niki::syntax::ASTNodeIndex node_idx);
    RegId allocVReg(BuildCtx &bc, FuncCtx &fc);
    //---底层指令发射接口---
    void emit(BuildCtx &bc, FuncCtx &fc, InstKind k, ValueKind dk, uint32_t du32, int64_t di64, uint64_t du64,
              ValueKind ak, uint32_t au32, int64_t ai64, uint64_t au64, ValueKind bk, uint32_t bu32, int64_t bi64,
              uint64_t bu64, ValueKind ck, uint32_t cu32, int64_t ci64, uint64_t cu64, uint32_t aux = 0);
    uint32_t emitInstId(BuildCtx &bc, FuncCtx &fc, InstKind k, ValueKind dk, uint32_t du32, int64_t di64, uint64_t du64,
                        ValueKind ak, uint32_t au32, int64_t ai64, uint64_t au64, ValueKind bk, uint32_t bu32,
                        int64_t bi64, uint64_t bu64, ValueKind ck, uint32_t cu32, int64_t ci64, uint64_t cu64,
                        uint32_t aux = 0);
    //---常用语义发射封装---
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
    //---块终结与错误上报辅助---
    void emitReturnInvalid(BuildCtx &bc, FuncCtx &fc);
    bool isCurrentBlockTerminated(BuildCtx &bc, FuncCtx &fc);
    bool ensureBlockTerminated(BuildCtx &bc, FuncCtx &fc);
    void error(BuildCtx &bc, const std::string &msg, niki::syntax::ASTNodeIndex idx);
};
} // namespace niki::ir