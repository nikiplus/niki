#pragma once
#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/semantic/global_compilation.hpp"
#include "niki/l0_core/semantic/global_symbol_table.hpp"
#include "niki/l0_core/semantic/global_type_arena.hpp"
#include "niki/l0_core/syntax/ast_payloads.hpp"
#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace niki::ir {

// IRBuilder 负责把“语法树节点 + 语义阶段产物”降解为 ModuleIR。
// 设计边界：
// - 不做字节码发射（那是 lowering 的职责）
// - 不做最终结构合法性判定（那是 verify 的职责）
// - 只负责“可解释的中间语义形状”构建与最小错误上报
class IRBuilder {
  public:
    // 最小入口：只依赖 unit 本身（单文件/单模块场景）
    std::expected<ModuleIR, diagnostic::DiagnosticBag> build(GlobalCompilationUnit &unit);

    // 扩展入口：可选接入全局语义表（跨文件符号/签名约束扩展点）
    std::expected<ModuleIR, diagnostic::DiagnosticBag> build(GlobalCompilationUnit &unit,
                                                             const GlobalSymbolTable *global_symbols,
                                                             const GlobalTypeArena *global_arena);

  private:
    // 单层循环的补丁信息：
    // - continue_target_block: continue 直接跳转目标（通常是 loop.cond）
    // - break_sources: break 发射时先写占位 Jump，循环收束后统一回填到 loop.exit
    struct LoopPatch {
        IRBlockId continue_target_block = std::numeric_limits<IRBlockId>::max();
        std::vector<IRBlockId> break_sources;
    };

    // 函数级构建上下文：
    // - current_block: 当前 IR 写入位置
    // - local_reg_by_name_id: 作用域内名字到虚拟寄存器映射
    // - loop_stack: 嵌套循环补丁栈（支持 break/continue 语义）
    struct FuncBuildCtx {
        IRFunction *func = nullptr;
        IRBasicBlock *current_block = nullptr;
        std::unordered_map<uint32_t, IRRegId> local_reg_by_name_id;
        std::vector<LoopPatch> loop_stack;
    };

    GlobalCompilationUnit *unit_ = nullptr;
    ModuleIR module_ir_;
    diagnostic::DiagnosticBag diags_;
    const GlobalSymbolTable *global_syms_ = nullptr;
    const GlobalTypeArena *global_arena_ = nullptr;

    //---high-level pipeline---
    bool buildModuleRoot();
    bool buildTopLvDecl(syntax::ASTNodeIndex decl_idx);
    bool buildFuncDecl(syntax::ASTNodeIndex decl_idx);
    bool buildStructDecl(syntax::ASTNodeIndex decl_idx);

    //---statement / expression lowering---
    // 约定：statement 负责控制流骨架；expression 负责产出可消费寄存器值。
    bool buildStmt(FuncBuildCtx &ctx, syntax::ASTNodeIndex stmt_idx);
    bool buildExpr(FuncBuildCtx &ctx, syntax::ASTNodeIndex expr_idx, IRRegId *out_reg);

    bool buildIfStmt(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex stmt_idx);
    bool buildLoopStmt(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex stmt_idx);
    bool buildReturnStmt(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex stmt_idx);
    bool buildVarDeclStmt(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex stmt_idx);
    bool buildAssignmentStmt(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex stmt_idx);
    bool buildExprStmt(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex stmt_idx);
    bool buildBreakStmt(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex stmt_idx);
    bool buildContinueStmt(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex stmt_idx);

    bool buildLiteralExpr(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex expr_idx, IRRegId *out_reg);
    bool buildIdentifierExpr(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex expr_idx, IRRegId *out_reg);
    bool buildBinaryExpr(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex expr_idx, IRRegId *out_reg);
    bool buildUnaryExpr(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex expr_idx, IRRegId *out_reg);
    bool buildCallExpr(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex expr_idx, IRRegId *out_reg);
    bool buildArrayExpr(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex expr_idx, IRRegId *out_reg);
    bool buildMapExpr(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex expr_idx, IRRegId *out_reg);
    bool buildIndexExpr(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex expr_idx, IRRegId *out_reg);
    bool buildMemberExpr(FuncBuildCtx &ctx, niki::syntax::ASTNodeIndex expr_idx, IRRegId *out_reg);

    //---helpers---
    // emit* 系列只负责“写指令”，不做高层语义判断。
    void reportError(const std::string &message, syntax::ASTNodeIndex node_idx);
    IRBasicBlock &appendBlock(FuncBuildCtx &ctx, const std::string &debug_name);
    void switchToBlock(FuncBuildCtx &ctx, IRBasicBlock &block);

    void emitInst(FuncBuildCtx &ctx, const IRInst &inst);
    void emitJump(FuncBuildCtx &ctx, IRBlockId target_block_id);
    void emitBranch(FuncBuildCtx &ctx, IRRegId cond_reg, IRBlockId true_block_id, IRBlockId false_block_id);
    void emitReturn(FuncBuildCtx &ctx, const IRValue &ret_val);

    bool ensureBlockTerminated(FuncBuildCtx &ctx);
    bool isCurrentBlockTerminated(const FuncBuildCtx &ctx) const;

    IRSymbolId ensureSymbol(uint32_t name_id, IRSymbolKind sym_kind, const IRType &sym_type, bool is_exported);
};
} // namespace niki::ir