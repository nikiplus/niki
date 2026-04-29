#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include <limits>

/** @builder_stmt_impl: 语句到控制流图的降解实现
 * 这个文件负责把语句节点转换成 IR 的控制流骨架与状态更新指令。
 * 与表达式层“产值到寄存器”不同，语句层主要负责“程序如何走”：
 * 块作用域、分支汇合、循环回边、break/continue 跳转、return 终止。
 *
 * 这里的关键实现点是控制流重写：
 * AST 中的 if/loop 是嵌套节点，IR/VM 里需要显式块与显式跳转。
 * 因此本文件通过 then/else/join、cond/body/exit 等块模板构造 CFG，
 * 并用占位 jump + patch 的方式处理需要后定目标的 break。
 *
 * 这个文件的正确性直接决定 verify 阶段能否通过“终结符在块尾”等结构不变量检查。
 *
 * 字段流说明（核心）：
 * - 输入语句节点 `stmt_idx` -> `setEmitLocation`：先更新当前指令源码映射位置。
 * - 局部声明 `name_sid` -> `fc.local_vreg_by_name_sid`：建立名字到寄存器绑定。
 * - 控制流语句 -> `beginBlock/switchBlock`：把树形结构重写为块图结构。
 * - `break` 指令 id -> `fc.loop_stack.back().break_jump_inst_ids`：延迟到 loop 结束统一回填出口块。
 * - 语句末尾 -> `ensureBlockTerminated`：兜底补终结符，保证块结构闭合。
 */
namespace niki::ir {
using namespace niki::syntax;

namespace {
constexpr uint32_t kInvalidBlockId = std::numeric_limits<uint32_t>::max();

/**
 * @brief 将复合赋值操作符映射到对应 IR 指令。
 * @param token_type 复合赋值 token 类型。
 * @return InstKind 映射成功返回算术指令，未知返回 InstKind::Nop。
 */
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
/**
 * @brief 将语句节点降级为 IR 指令与控制流块。
 * @param bc 构建上下文。
 * @param fc 函数上下文。
 * @param stmt_idx 语句节点索引。
 * @return true 语句降级成功。
 * @return false 出现不支持语句、非法跳转或子表达式降级失败。
 */
bool IRBuilder::buildStmt(BuildCtx &bc, FuncCtx &fc, ASTNodeIndex stmt_idx) {
    if (!stmt_idx.isvalid()) {
        return true;
    }

    setEmitLocation(bc, fc, stmt_idx);
    const ASTNode &stmt = bc.unit->pool.getNode(stmt_idx);

    // DISPATCH_RULE:
    // - 表达式语句走值流（buildExpr）；
    // - 控制语句走块流（beginBlock/switchBlock/jump/branch）；
    // - 声明语句同时更新符号寄存器映射（local_vreg_by_name_sid）。
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
            setEmitLocation(bc, fc, stmt_idx);
            emitMoveRegToReg(bc, fc, dst, init_reg);
        } else {
            setEmitLocation(bc, fc, stmt_idx);
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
        setEmitLocation(bc, fc, stmt_idx);
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
        setEmitLocation(bc, fc, stmt_idx);
        const BlockId then_block_id = beginBlock(bc, fc, "if.then");
        const BlockId join_block_id = beginBlock(bc, fc, "if.join");
        const bool has_else = stmt.payload.if_stmt.else_branch.isvalid();
        BlockId else_block_id = join_block_id;
        if (has_else) {
            else_block_id = beginBlock(bc, fc, "if.else");
        }
        emitBranchOnReg(bc, fc, cond_reg, then_block_id, else_block_id);

        // then 分支发射
        switchBlock(fc, then_block_id);
        bool ok = buildStmt(bc, fc, stmt.payload.if_stmt.then_branch);
        if (!isCurrentBlockTerminated(bc, fc)) {
            setEmitLocation(bc, fc, stmt_idx);
            emitJumpToBlock(bc, fc, join_block_id);
        }

        if (has_else) {
            // else 分支发射（存在时）
            switchBlock(fc, else_block_id);
            ok = buildStmt(bc, fc, stmt.payload.if_stmt.else_branch) && ok;
            if (!isCurrentBlockTerminated(bc, fc)) {
                setEmitLocation(bc, fc, stmt_idx);
                emitJumpToBlock(bc, fc, join_block_id);
            }
        }

        // 分支汇合点：后续语句从 join 继续发射。
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
            setEmitLocation(bc, fc, stmt_idx);
            emitBranchOnReg(bc, fc, cond_reg, loop_body_block_id, loop_exit_block_id);
        } else {
            setEmitLocation(bc, fc, stmt_idx);
            emitJumpToBlock(bc, fc, loop_body_block_id);
        }

        switchBlock(fc, loop_body_block_id);
        bool ok = buildStmt(bc, fc, stmt.payload.loop.body);
        if (!isCurrentBlockTerminated(bc, fc)) {
            setEmitLocation(bc, fc, stmt_idx);
            emitJumpToBlock(bc, fc, loop_condition_block_id);
        }

        // 统一回填本层 loop 中收集到的 break 占位跳转。
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
            setEmitLocation(bc, fc, stmt_idx);
            emit(bc, fc, InstKind::Return, ValueKind::Invalid, 0, 0, 0, ValueKind::VReg, ret, 0, 0, ValueKind::Invalid,
                 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
        } else {
            setEmitLocation(bc, fc, stmt_idx);
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
