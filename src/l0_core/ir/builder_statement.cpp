#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include <limits>

namespace niki::ir {
using namespace niki::syntax;

namespace {
constexpr uint32_t kInvalidBlockId = std::numeric_limits<uint32_t>::max();

InstKind mapCompoundAssignTokenToInst(TokenType token_type) {
    switch (token_type) {
    case TokenType::SYM_PLUS_EQUAL:
        return InstKind::Add;
    case TokenType::SYM_MINUS_EQUAL:
        return InstKind::Sub;
    case TokenType::SYM_STAR_EQUAL:
        return InstKind::Mul;
    case TokenType::SYM_SLASH_EQUAL:
        return InstKind::Div;
    case TokenType::SYM_MOD_EQUAL:
        return InstKind::Mod;
    default:
        return InstKind::Nop;
    }
}
} // namespace

//------------------------------------------------------------------------------
// STMT_ENTRY: 语句降解入口，按节点类型分发到对应发射逻辑。
//------------------------------------------------------------------------------
bool IRBuilder::buildStmt(BuildCtx &bc, FuncCtx &fc, ASTNodeIndex stmt_idx) {
    if (!stmt_idx.isvalid()) {
        return true;
    }
    const ASTNode &stmt = bc.unit->pool.getNode(stmt_idx);
    switch (stmt.type) {
    case NodeType::BlockStmt: {
        auto statements = bc.unit->pool.get_list(stmt.payload.list.elements);
        bool ok = true;
        for (ASTNodeIndex one_stmt : statements) {
            if (isCurrentBlockTerminated(bc, fc)) {
                break;
            }
            ok = buildStmt(bc, fc, one_stmt) && ok;
        }
        return ok;
    }
    case NodeType::ExpressionStmt: {
        RegId ignored_result_reg = 0;
        return buildExpr(bc, fc, stmt.payload.expr_stmt.expression, &ignored_result_reg);
    }
    case NodeType::VarDeclStmt:
    case NodeType::ConstDeclStmt: {
        // VAR_DECL: 变量声明降解。
        // STEP: 分配目标寄存器。
        // STEP: 若有初始化表达式则先求值。
        // STEP: 将求值结果写入目标寄存器。
        const uint32_t name_sid = stmt.payload.var_decl.name_id;
        const RegId dst = allocVReg(bc, fc);
        fc.local_vreg_by_name_sid[name_sid] = dst;
        if (stmt.payload.var_decl.init_expr.isvalid()) {
            RegId init_reg = 0;
            if (!buildExpr(bc, fc, stmt.payload.var_decl.init_expr, &init_reg)) {
                return false;
            }
            emitMoveRegToReg(bc, fc, dst, init_reg);
        } else {
            emit(bc, fc, InstKind::Constant, ValueKind::VReg, dst, 0, 0, ValueKind::Invalid, 0, 0, 0,
                 ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
        }
        return true;
    }
    case NodeType::AssignmentStmt: {
        // ASSIGN_LIMIT: 当前阶段仅支持标识符左值赋值。
        const ASTNode &target = bc.unit->pool.getNode(stmt.payload.assign_stmt.target);
        const TokenType assign_op = stmt.payload.assign_stmt.op;
        if (target.type != NodeType::IdentifierExpr) {
            error(bc, "Only identifier assignment is supported in current flat IR builder.", stmt_idx);
            return false;
        }
        auto it = fc.local_vreg_by_name_sid.find(target.payload.identifier.name_id);
        if (it == fc.local_vreg_by_name_sid.end()) {
            error(bc, "Assignment target is undefined in current function scope.", stmt_idx);
            return false;
        }
        RegId rhs = 0;
        if (!buildExpr(bc, fc, stmt.payload.assign_stmt.value, &rhs)) {
            return false;
        }
        if (assign_op == TokenType::SYM_EQUAL) {
            emitMoveRegToReg(bc, fc, it->second, rhs);
            return true;
        }
        const InstKind op_inst = mapCompoundAssignTokenToInst(assign_op);
        if (op_inst == InstKind::Nop) {
            error(bc, "Unsupported compound assignment operator.", stmt_idx);
            return false;
        }
        const RegId tmp = allocVReg(bc, fc);
        emitBinaryReg(bc, fc, op_inst, tmp, it->second, rhs);
        emitMoveRegToReg(bc, fc, it->second, tmp);
        return true;
    }
    case NodeType::IfStmt: {
        // IF_CFG: if 语句控制流骨架。
        // FLOW: current -> branch(cond, then, else/join) -> join。
        RegId cond_reg = 0;
        if (!buildExpr(bc, fc, stmt.payload.if_stmt.condition, &cond_reg)) {
            return false;
        }
        const BlockId then_block_id = beginBlock(bc, fc, "if.then");
        const BlockId join_block_id = beginBlock(bc, fc, "if.join");
        const bool has_else = stmt.payload.if_stmt.else_branch.isvalid();
        BlockId else_block_id = join_block_id;
        if (has_else) {
            else_block_id = beginBlock(bc, fc, "if.else");
        }
        emitBranchOnReg(bc, fc, cond_reg, then_block_id, else_block_id);

        switchBlock(fc, then_block_id);
        bool ok = buildStmt(bc, fc, stmt.payload.if_stmt.then_branch);
        if (!isCurrentBlockTerminated(bc, fc)) {
            emitJumpToBlock(bc, fc, join_block_id);
        }

        if (has_else) {
            switchBlock(fc, else_block_id);
            ok = buildStmt(bc, fc, stmt.payload.if_stmt.else_branch) && ok;
            if (!isCurrentBlockTerminated(bc, fc)) {
                emitJumpToBlock(bc, fc, join_block_id);
            }
        }

        switchBlock(fc, join_block_id);
        return ok;
    }
    case NodeType::LoopStmt: {
        // LOOP_CFG: loop 语句标准控制流骨架。
        // FLOW: current -> cond -> body -> cond，且 cond 可分叉到 exit。
        // PATCH: break 的 jump 先写占位，body 降解结束后统一回填到 exit。
        const BlockId loop_condition_block_id = beginBlock(bc, fc, "loop.cond");
        const BlockId loop_body_block_id = beginBlock(bc, fc, "loop.body");
        const BlockId loop_exit_block_id = beginBlock(bc, fc, "loop.exit");

        emitJumpToBlock(bc, fc, loop_condition_block_id);

        fc.loop_stack.push_back(LoopPatch{.continue_target = loop_condition_block_id, .break_jump_inst_ids = {}});

        switchBlock(fc, loop_condition_block_id);
        if (stmt.payload.loop.condition.isvalid()) {
            RegId cond_reg = 0;
            if (!buildExpr(bc, fc, stmt.payload.loop.condition, &cond_reg)) {
                return false;
            }
            emitBranchOnReg(bc, fc, cond_reg, loop_body_block_id, loop_exit_block_id);
        } else {
            emitJumpToBlock(bc, fc, loop_body_block_id);
        }

        switchBlock(fc, loop_body_block_id);
        bool ok = buildStmt(bc, fc, stmt.payload.loop.body);
        if (!isCurrentBlockTerminated(bc, fc)) {
            emitJumpToBlock(bc, fc, loop_condition_block_id);
        }

        const LoopPatch loop_patch = fc.loop_stack.back();
        fc.loop_stack.pop_back();
        for (uint32_t break_jump_inst_id : loop_patch.break_jump_inst_ids) {
            if (break_jump_inst_id < bc.module.insts.a_kind.size() &&
                bc.module.insts.a_kind[break_jump_inst_id] == ValueKind::BlockId) {
                bc.module.insts.a_u32[break_jump_inst_id] = loop_exit_block_id;
            }
        }

        switchBlock(fc, loop_exit_block_id);
        return ok;
    }
    case NodeType::BreakStmt: {
        if (fc.loop_stack.empty()) {
            error(bc, "break used outside loop.", stmt_idx);
            return false;
        }
        const uint32_t break_jump_inst_id = emitJumpPlaceholder(bc, fc, kInvalidBlockId);
        fc.loop_stack.back().break_jump_inst_ids.push_back(break_jump_inst_id);
        const BlockId dead_block_id = beginBlock(bc, fc, "dead.after_break");
        switchBlock(fc, dead_block_id);
        return true;
    }
    case NodeType::ContinueStmt: {
        if (fc.loop_stack.empty()) {
            error(bc, "continue used outside loop.", stmt_idx);
            return false;
        }
        emitJumpToBlock(bc, fc, fc.loop_stack.back().continue_target);
        const BlockId dead_block_id = beginBlock(bc, fc, "dead.after_continue");
        switchBlock(fc, dead_block_id);
        return true;
    }
    case NodeType::ReturnStmt: {
        // RETURN: 支持可选返回值表达式。
        if (stmt.payload.return_stmt.expression.isvalid()) {
            RegId ret = 0;
            if (!buildExpr(bc, fc, stmt.payload.return_stmt.expression, &ret)) {
                return false;
            }
            emit(bc, fc, InstKind::Return, ValueKind::Invalid, 0, 0, 0, ValueKind::VReg, ret, 0, 0, ValueKind::Invalid, 0, 0,
                 0, ValueKind::Invalid, 0, 0, 0);
        } else {
            emitReturnInvalid(bc, fc);
        }
        return true;
    }
    default:
        error(bc, "Statement node is not supported by current flat IR builder.", stmt_idx);
        return false;
    }
}

} // namespace niki::ir
