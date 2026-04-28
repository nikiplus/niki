#include "niki/l0_core/ir/builder.hpp"

#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include <cstdint>
#include <limits>

namespace niki::ir {
using namespace niki::syntax;

namespace {
constexpr IRBlockId kInvalidBlockId = std::numeric_limits<IRBlockId>::max();

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

IRInstKind mapCompoundAssignTokenToInst(TokenType token_type) {
    switch (token_type) {
    case TokenType::SYM_PLUS_EQUAL:
        return IRInstKind::Add;
    case TokenType::SYM_MINUS_EQUAL:
        return IRInstKind::Sub;
    case TokenType::SYM_STAR_EQUAL:
        return IRInstKind::Mul;
    case TokenType::SYM_SLASH_EQUAL:
        return IRInstKind::Div;
    case TokenType::SYM_MOD_EQUAL:
        return IRInstKind::Mod;
    default:
        return IRInstKind::Nop;
    }
}
} // namespace

bool IRBuilder::buildStmt(FuncBuildCtx &ctx, ASTNodeIndex stmt_idx) {
    if (!stmt_idx.isvalid()) {
        return true;
    }
    const ASTNode &stmt = unit_->pool.getNode(stmt_idx);
    switch (stmt.type) {
    case NodeType::BlockStmt: {
        auto statements = unit_->pool.get_list(stmt.payload.list.elements);
        bool ok = true;
        for (ASTNodeIndex one_stmt : statements) {
            if (isCurrentBlockTerminated(ctx)) {
                break;
            }
            ok = buildStmt(ctx, one_stmt) && ok;
        }
        return ok;
    }
    case NodeType::ExpressionStmt:
        return buildExprStmt(ctx, stmt_idx);
    case NodeType::VarDeclStmt:
    case NodeType::ConstDeclStmt:
        return buildVarDeclStmt(ctx, stmt_idx);
    case NodeType::AssignmentStmt:
        return buildAssignmentStmt(ctx, stmt_idx);
    case NodeType::IfStmt:
        return buildIfStmt(ctx, stmt_idx);
    case NodeType::LoopStmt:
        return buildLoopStmt(ctx, stmt_idx);
    case NodeType::MatchStmt:
    case NodeType::MatchCaseStmt:
    case NodeType::NockStmt:
    case NodeType::AttachStmt:
    case NodeType::DetachStmt:
    case NodeType::TargetStmt:
        reportError("Statement node is recognized but not implemented in IRBuilder yet.", stmt_idx);
        return false;
    case NodeType::BreakStmt:
        return buildBreakStmt(ctx, stmt_idx);
    case NodeType::ContinueStmt:
        return buildContinueStmt(ctx, stmt_idx);
    case NodeType::ReturnStmt:
        return buildReturnStmt(ctx, stmt_idx);
    default:
        reportError("Statement node is not supported by IRBuilder.", stmt_idx);
        return false;
    }
}

bool IRBuilder::buildExprStmt(FuncBuildCtx &ctx, ASTNodeIndex stmt_idx) {
    const ASTNode &stmt = unit_->pool.getNode(stmt_idx);
    IRRegId sink = 0;
    return buildExpr(ctx, stmt.payload.expr_stmt.expression, &sink);
}

bool IRBuilder::buildVarDeclStmt(FuncBuildCtx &ctx, ASTNodeIndex stmt_idx) {
    const ASTNode &stmt = unit_->pool.getNode(stmt_idx);
    uint32_t name_id = stmt.payload.var_decl.name_id;

    IRRegId dst = ctx.func->allocateVirtualRegister();
    ctx.local_reg_by_name_id[name_id] = dst;

    if (stmt.payload.var_decl.init_expr.isvalid()) {
        IRRegId init_reg = 0;
        if (!buildExpr(ctx, stmt.payload.var_decl.init_expr, &init_reg)) {
            return false;
        }
        emitInst(ctx, makeSimpleInst(IRInstKind::Move, IRValue::makeVirtualRegisterValue(dst),
                                     IRValue::makeVirtualRegisterValue(init_reg)));
    } else {
        emitInst(ctx, makeSimpleInst(IRInstKind::Constant, IRValue::makeVirtualRegisterValue(dst)));
    }
    return true;
}

bool IRBuilder::buildAssignmentStmt(FuncBuildCtx &ctx, ASTNodeIndex stmt_idx) {
    const ASTNode &stmt = unit_->pool.getNode(stmt_idx);
    const ASTNode &target = unit_->pool.getNode(stmt.payload.assign_stmt.target);
    const TokenType assign_operator = stmt.payload.assign_stmt.op;

    if (target.type == NodeType::IndexExpr) {
        IRRegId target_reg = 0;
        IRRegId index_reg = 0;
        if (!buildExpr(ctx, target.payload.index.target, &target_reg) ||
            !buildExpr(ctx, target.payload.index.index, &index_reg)) {
            return false;
        }

        IRRegId write_reg = 0;
        if (assign_operator == TokenType::SYM_EQUAL) {
            if (!buildExpr(ctx, stmt.payload.assign_stmt.value, &write_reg)) {
                return false;
            }
        } else {
            IRInstKind compound_inst = mapCompoundAssignTokenToInst(assign_operator);
            if (compound_inst == IRInstKind::Nop) {
                reportError("Assignment operator is recognized but not implemented in IRBuilder yet.", stmt_idx);
                return false;
            }
            IRRegId old_reg = ctx.func->allocateVirtualRegister();
            emitInst(ctx, makeSimpleInst(IRInstKind::GetIndex, IRValue::makeVirtualRegisterValue(old_reg),
                                         IRValue::makeVirtualRegisterValue(target_reg),
                                         IRValue::makeVirtualRegisterValue(index_reg)));

            IRRegId rhs_reg = 0;
            if (!buildExpr(ctx, stmt.payload.assign_stmt.value, &rhs_reg)) {
                return false;
            }
            write_reg = ctx.func->allocateVirtualRegister();
            emitInst(ctx, makeSimpleInst(compound_inst, IRValue::makeVirtualRegisterValue(write_reg),
                                         IRValue::makeVirtualRegisterValue(old_reg),
                                         IRValue::makeVirtualRegisterValue(rhs_reg)));
        }

        emitInst(ctx, makeSimpleInst(IRInstKind::SetIndex, IRValue::makeInvalid(),
                                     IRValue::makeVirtualRegisterValue(target_reg),
                                     IRValue::makeVirtualRegisterValue(index_reg),
                                     IRValue::makeVirtualRegisterValue(write_reg)));
        return true;
    }
    if (target.type == NodeType::MemberExpr) {
        IRRegId object_reg = 0;
        if (!buildExpr(ctx, target.payload.member.object, &object_reg)) {
            return false;
        }

        IRValue member_name = IRValue::makeStringIdentifierValue(target.payload.member.property_id);
        IRRegId write_reg = 0;
        if (assign_operator == TokenType::SYM_EQUAL) {
            if (!buildExpr(ctx, stmt.payload.assign_stmt.value, &write_reg)) {
                return false;
            }
        } else {
            IRInstKind compound_inst = mapCompoundAssignTokenToInst(assign_operator);
            if (compound_inst == IRInstKind::Nop) {
                reportError("Assignment operator is recognized but not implemented in IRBuilder yet.", stmt_idx);
                return false;
            }

            IRRegId old_reg = ctx.func->allocateVirtualRegister();
            emitInst(ctx, makeSimpleInst(IRInstKind::GetMember, IRValue::makeVirtualRegisterValue(old_reg),
                                         IRValue::makeVirtualRegisterValue(object_reg), member_name));

            IRRegId rhs_reg = 0;
            if (!buildExpr(ctx, stmt.payload.assign_stmt.value, &rhs_reg)) {
                return false;
            }
            write_reg = ctx.func->allocateVirtualRegister();
            emitInst(ctx, makeSimpleInst(compound_inst, IRValue::makeVirtualRegisterValue(write_reg),
                                         IRValue::makeVirtualRegisterValue(old_reg),
                                         IRValue::makeVirtualRegisterValue(rhs_reg)));
        }

        emitInst(ctx, makeSimpleInst(IRInstKind::SetMember, IRValue::makeInvalid(),
                                     IRValue::makeVirtualRegisterValue(object_reg), member_name,
                                     IRValue::makeVirtualRegisterValue(write_reg)));
        return true;
    }

    if (target.type != NodeType::IdentifierExpr) {
        reportError("Only identifier assignment is supported in IRBuilder.", stmt_idx);
        return false;
    }

    uint32_t name_id = target.payload.identifier.name_id;
    auto local_iter = ctx.local_reg_by_name_id.find(name_id);
    if (local_iter == ctx.local_reg_by_name_id.end()) {
        reportError("Assignment target is undefined in current function scope.", stmt_idx);
        return false;
    }

    IRRegId rhs_reg = 0;
    if (!buildExpr(ctx, stmt.payload.assign_stmt.value, &rhs_reg)) {
        return false;
    }

    if (assign_operator == TokenType::SYM_EQUAL) {
        emitInst(ctx, makeSimpleInst(IRInstKind::Move, IRValue::makeVirtualRegisterValue(local_iter->second),
                                     IRValue::makeVirtualRegisterValue(rhs_reg)));
        return true;
    }

    IRInstKind compound_inst = mapCompoundAssignTokenToInst(assign_operator);
    if (compound_inst == IRInstKind::Nop) {
        reportError("Assignment operator is recognized but not implemented in IRBuilder yet.", stmt_idx);
        return false;
    }

    IRRegId result_reg = ctx.func->allocateVirtualRegister();
    emitInst(ctx, makeSimpleInst(compound_inst, IRValue::makeVirtualRegisterValue(result_reg),
                                 IRValue::makeVirtualRegisterValue(local_iter->second),
                                 IRValue::makeVirtualRegisterValue(rhs_reg)));
    emitInst(ctx, makeSimpleInst(IRInstKind::Move, IRValue::makeVirtualRegisterValue(local_iter->second),
                                 IRValue::makeVirtualRegisterValue(result_reg)));
    return true;
}

bool IRBuilder::buildReturnStmt(FuncBuildCtx &ctx, ASTNodeIndex stmt_idx) {
    const ASTNode &stmt = unit_->pool.getNode(stmt_idx);
    if (stmt.payload.return_stmt.expression.isvalid()) {
        IRRegId ret_reg = 0;
        if (!buildExpr(ctx, stmt.payload.return_stmt.expression, &ret_reg)) {
            return false;
        }
        emitReturn(ctx, IRValue::makeVirtualRegisterValue(ret_reg));
    } else {
        emitReturn(ctx, IRValue::makeInvalid());
    }
    return true;
}

bool IRBuilder::buildIfStmt(FuncBuildCtx &ctx, ASTNodeIndex stmt_idx) {
    const ASTNode &stmt = unit_->pool.getNode(stmt_idx);
    const bool has_else_branch = stmt.payload.if_stmt.else_branch.isvalid();

    IRRegId cond_reg = 0;
    if (!buildExpr(ctx, stmt.payload.if_stmt.condition, &cond_reg)) {
        return false;
    }

    const IRBlockId then_block_id = appendBlock(ctx, "if.then").block_id;
    const IRBlockId join_block_id = appendBlock(ctx, "if.join").block_id;
    IRBlockId else_block_id = kInvalidBlockId;
    if (has_else_branch) {
        else_block_id = appendBlock(ctx, "if.else").block_id;
    }

    IRBlockId false_target = has_else_branch ? else_block_id : join_block_id;
    emitBranch(ctx, cond_reg, then_block_id, false_target);

    IRBasicBlock *then_block = findBlockById(*ctx.func, then_block_id);
    if (then_block == nullptr) {
        reportError("Failed to resolve then block during if lowering.", stmt_idx);
        return false;
    }
    switchToBlock(ctx, *then_block);
    bool ok = buildStmt(ctx, stmt.payload.if_stmt.then_branch);
    if (!isCurrentBlockTerminated(ctx)) {
        emitJump(ctx, join_block_id);
    }

    if (has_else_branch) {
        IRBasicBlock *else_block = findBlockById(*ctx.func, else_block_id);
        if (else_block == nullptr) {
            reportError("Failed to resolve else block during if lowering.", stmt_idx);
            return false;
        }
        switchToBlock(ctx, *else_block);
        ok = buildStmt(ctx, stmt.payload.if_stmt.else_branch) && ok;
        if (!isCurrentBlockTerminated(ctx)) {
            emitJump(ctx, join_block_id);
        }
    }

    IRBasicBlock *join_block = findBlockById(*ctx.func, join_block_id);
    if (join_block == nullptr) {
        reportError("Failed to resolve join block during if lowering.", stmt_idx);
        return false;
    }
    switchToBlock(ctx, *join_block);
    return ok;
}

bool IRBuilder::buildLoopStmt(FuncBuildCtx &ctx, ASTNodeIndex stmt_idx) {
    const ASTNode &stmt = unit_->pool.getNode(stmt_idx);

    const IRBlockId cond_block_id = appendBlock(ctx, "loop.cond").block_id;
    const IRBlockId body_block_id = appendBlock(ctx, "loop.body").block_id;
    const IRBlockId exit_block_id = appendBlock(ctx, "loop.exit").block_id;

    emitJump(ctx, cond_block_id);
    ctx.loop_stack.push_back(LoopPatch{.continue_target_block = cond_block_id, .break_sources = {}});

    IRBasicBlock *cond_block = findBlockById(*ctx.func, cond_block_id);
    if (cond_block == nullptr) {
        reportError("Failed to resolve loop condition block.", stmt_idx);
        return false;
    }
    switchToBlock(ctx, *cond_block);
    if (stmt.payload.loop.condition.isvalid()) {
        IRRegId cond_reg = 0;
        if (!buildExpr(ctx, stmt.payload.loop.condition, &cond_reg)) {
            return false;
        }
        emitBranch(ctx, cond_reg, body_block_id, exit_block_id);
    } else {
        emitJump(ctx, body_block_id);
    }

    IRBasicBlock *body_block = findBlockById(*ctx.func, body_block_id);
    if (body_block == nullptr) {
        reportError("Failed to resolve loop body block.", stmt_idx);
        return false;
    }
    switchToBlock(ctx, *body_block);
    bool ok = buildStmt(ctx, stmt.payload.loop.body);
    if (!isCurrentBlockTerminated(ctx)) {
        emitJump(ctx, cond_block_id);
    }

    LoopPatch patch = ctx.loop_stack.back();
    ctx.loop_stack.pop_back();
    for (IRBlockId break_src_block_id : patch.break_sources) {
        IRBasicBlock *src_block = findBlockById(*ctx.func, break_src_block_id);
        if (src_block == nullptr || src_block->instruction_list.empty()) {
            continue;
        }
        IRInst &last_inst = src_block->instruction_list.back();
        if (last_inst.instruction_kind == IRInstKind::Jump &&
            last_inst.first_operand.value_kind == IRValueKind::BlockId &&
            last_inst.first_operand.payload_as_u32 == kInvalidBlockId) {
            last_inst.first_operand = IRValue::makeBlockIdentifierValue(exit_block_id);
        }
    }

    IRBasicBlock *exit_block = findBlockById(*ctx.func, exit_block_id);
    if (exit_block == nullptr) {
        reportError("Failed to resolve loop exit block.", stmt_idx);
        return false;
    }
    switchToBlock(ctx, *exit_block);
    return ok;
}

bool IRBuilder::buildBreakStmt(FuncBuildCtx &ctx, ASTNodeIndex stmt_idx) {
    if (ctx.loop_stack.empty()) {
        reportError("break used outside loop.", stmt_idx);
        return false;
    }

    emitJump(ctx, kInvalidBlockId);
    ctx.loop_stack.back().break_sources.push_back(ctx.current_block->block_id);

    IRBasicBlock &dead_block = appendBlock(ctx, "dead.after_break");
    switchToBlock(ctx, dead_block);
    return true;
}

bool IRBuilder::buildContinueStmt(FuncBuildCtx &ctx, ASTNodeIndex stmt_idx) {
    if (ctx.loop_stack.empty()) {
        reportError("continue used outside loop.", stmt_idx);
        return false;
    }

    emitJump(ctx, ctx.loop_stack.back().continue_target_block);

    IRBasicBlock &dead_block = appendBlock(ctx, "dead.after_continue");
    switchToBlock(ctx, dead_block);
    return true;
}

} // namespace niki::ir
