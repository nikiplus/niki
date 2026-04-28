#include "niki/l0_core/ir/builder.hpp"

#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include "niki/l0_core/vm/object.hpp"
#include "niki/l0_core/vm/value.hpp"

#include <vector>

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

IRInstKind mapBinaryTokenToInst(TokenType token_type) {
    switch (token_type) {
    case TokenType::SYM_PLUS:
        return IRInstKind::Add;
    case TokenType::SYM_MINUS:
        return IRInstKind::Sub;
    case TokenType::SYM_STAR:
        return IRInstKind::Mul;
    case TokenType::SYM_SLASH:
        return IRInstKind::Div;
    case TokenType::SYM_MOD:
        return IRInstKind::Mod;
    case TokenType::SYM_EQUAL_EQUAL:
        return IRInstKind::CmpEq;
    case TokenType::SYM_BANG_EQUAL:
        return IRInstKind::CmpNe;
    case TokenType::SYM_LESS:
        return IRInstKind::CmpLt;
    case TokenType::SYM_LESS_EQUAL:
        return IRInstKind::CmpLe;
    case TokenType::SYM_GREATER:
        return IRInstKind::CmpGt;
    case TokenType::SYM_GREATER_EQUAL:
        return IRInstKind::CmpGe;
    case TokenType::SYM_AND:
        return IRInstKind::LogicAnd;
    case TokenType::SYM_OR:
        return IRInstKind::LogicOr;
    default:
        return IRInstKind::Nop;
    }
}
} // namespace

bool IRBuilder::buildExpr(FuncBuildCtx &ctx, ASTNodeIndex expr_idx, IRRegId *out_reg) {
    if (!expr_idx.isvalid()) {
        reportError("Invalid expression index.", expr_idx);
        return false;
    }

    const ASTNode &expr = unit_->pool.getNode(expr_idx);
    switch (expr.type) {
    case NodeType::LiteralExpr:
        return buildLiteralExpr(ctx, expr_idx, out_reg);
    case NodeType::IdentifierExpr:
        return buildIdentifierExpr(ctx, expr_idx, out_reg);
    case NodeType::BinaryExpr:
    case NodeType::LogicalExpr:
        return buildBinaryExpr(ctx, expr_idx, out_reg);
    case NodeType::UnaryExpr:
        return buildUnaryExpr(ctx, expr_idx, out_reg);
    case NodeType::CallExpr:
        return buildCallExpr(ctx, expr_idx, out_reg);
    case NodeType::ArrayExpr:
        return buildArrayExpr(ctx, expr_idx, out_reg);
    case NodeType::MapExpr:
        return buildMapExpr(ctx, expr_idx, out_reg);
    case NodeType::IndexExpr:
        return buildIndexExpr(ctx, expr_idx, out_reg);
    case NodeType::MemberExpr:
        return buildMemberExpr(ctx, expr_idx, out_reg);
    case NodeType::DispatchExpr:
    case NodeType::AwaitExpr:
    case NodeType::BorrowExpr:
    case NodeType::WildcardExpr:
    case NodeType::TypeExpr:
    case NodeType::ImplicitCastExpr:
        reportError("Expression node is recognized but not implemented in IRBuilder yet.", expr_idx);
        return false;
    default:
        reportError("Expression node is not supported by IRBuilder.", expr_idx);
        return false;
    }
}

bool IRBuilder::buildLiteralExpr(FuncBuildCtx &ctx, ASTNodeIndex expr_idx, IRRegId *out_reg) {
    const ASTNode &expr = unit_->pool.getNode(expr_idx);
    uint32_t const_idx = expr.payload.literal.const_pool_index;
    if (const_idx >= unit_->pool.constants.size()) {
        reportError("Literal const pool index out of range.", expr_idx);
        return false;
    }

    const vm::Value &literal = unit_->pool.constants[const_idx];
    IRRegId dst = ctx.func->allocateVirtualRegister();
    IRValue literal_value = IRValue::makeInvalid();

    switch (literal.type) {
    case vm::ValueType::Integer:
        literal_value = IRValue::makeImmediateIntegerValue(literal.as.integer);
        break;
    case vm::ValueType::Bool:
        literal_value = IRValue::makeImmediateBooleanValue(literal.as.boolean);
        break;
    case vm::ValueType::Object: {
        // 解析阶段字符串字面量以 ObjString 进入常量池；IR 侧用 StringId 统一承载。
        vm::Object *object_header = static_cast<vm::Object *>(literal.as.object);
        if (object_header == nullptr || object_header->type != vm::ObjType::String) {
            reportError("Object literal type is not supported in IRBuilder yet.", expr_idx);
            return false;
        }
        const vm::ObjString *string_object = static_cast<const vm::ObjString *>(literal.as.object);
        uint32_t string_id =
            unit_->pool.internString(std::string_view(string_object->chars, static_cast<size_t>(string_object->length)));
        literal_value = IRValue::makeStringIdentifierValue(string_id);
        break;
    }
    default:
        reportError("Literal type is not supported in IRBuilder yet.", expr_idx);
        return false;
    }

    emitInst(ctx, makeSimpleInst(IRInstKind::Constant, IRValue::makeVirtualRegisterValue(dst), literal_value));
    *out_reg = dst;
    return true;
}

bool IRBuilder::buildIdentifierExpr(FuncBuildCtx &ctx, ASTNodeIndex expr_idx, IRRegId *out_reg) {
    const ASTNode &expr = unit_->pool.getNode(expr_idx);
    uint32_t name_id = expr.payload.identifier.name_id;

    auto local_iter = ctx.local_reg_by_name_id.find(name_id);
    if (local_iter != ctx.local_reg_by_name_id.end()) {
        *out_reg = local_iter->second;
        return true;
    }

    IRSymbolId symbol_id = ensureSymbol(name_id, IRSymbolKind::External, IRType::makeUnknown(), false);
    IRRegId dst = ctx.func->allocateVirtualRegister();

    emitInst(ctx, makeSimpleInst(IRInstKind::LoadGlobal, IRValue::makeVirtualRegisterValue(dst),
                                 IRValue::makeSymbolIdentifierValue(symbol_id)));
    *out_reg = dst;
    return true;
}

bool IRBuilder::buildBinaryExpr(FuncBuildCtx &ctx, ASTNodeIndex expr_idx, IRRegId *out_reg) {
    const ASTNode &expr = unit_->pool.getNode(expr_idx);

    TokenType op = TokenType::TOKEN_ERROR;
    ASTNodeIndex left_idx = ASTNodeIndex::invalid();
    ASTNodeIndex right_idx = ASTNodeIndex::invalid();
    if (expr.type == NodeType::BinaryExpr) {
        op = expr.payload.binary.op;
        left_idx = expr.payload.binary.left;
        right_idx = expr.payload.binary.right;
    } else {
        op = expr.payload.logical.op;
        left_idx = expr.payload.logical.left;
        right_idx = expr.payload.logical.right;
    }

    IRRegId left_reg = 0;
    IRRegId right_reg = 0;
    if (!buildExpr(ctx, left_idx, &left_reg) || !buildExpr(ctx, right_idx, &right_reg)) {
        return false;
    }

    IRInstKind inst_kind = mapBinaryTokenToInst(op);
    if (inst_kind == IRInstKind::Nop) {
        reportError("Binary operator is not supported by IRBuilder yet.", expr_idx);
        return false;
    }

    IRRegId dst = ctx.func->allocateVirtualRegister();
    emitInst(ctx,
             makeSimpleInst(inst_kind, IRValue::makeVirtualRegisterValue(dst),
                            IRValue::makeVirtualRegisterValue(left_reg), IRValue::makeVirtualRegisterValue(right_reg)));
    *out_reg = dst;
    return true;
}

bool IRBuilder::buildUnaryExpr(FuncBuildCtx &ctx, ASTNodeIndex expr_idx, IRRegId *out_reg) {
    const ASTNode &expr = unit_->pool.getNode(expr_idx);
    TokenType op = expr.payload.unary.op;
    ASTNodeIndex operand_idx = expr.payload.unary.operand;

    IRRegId operand_reg = 0;
    if (!buildExpr(ctx, operand_idx, &operand_reg)) {
        return false;
    }

    // 一元 + 在语义上是 no-op，但这里仍显式复制到新寄存器，
    // 保持“表达式结果拥有独立目标寄存器”的构建约定。
    if (op == TokenType::SYM_PLUS) {
        IRRegId dst = ctx.func->allocateVirtualRegister();
        emitInst(ctx, makeSimpleInst(IRInstKind::Move, IRValue::makeVirtualRegisterValue(dst),
                                     IRValue::makeVirtualRegisterValue(operand_reg)));
        *out_reg = dst;
        return true;
    }

    IRInstKind inst_kind = IRInstKind::Nop;
    if (op == TokenType::SYM_MINUS) {
        inst_kind = IRInstKind::Neg;
    } else if (op == TokenType::SYM_BANG) {
        inst_kind = IRInstKind::LogicNot;
    } else {
        reportError("Unary operator is not supported by IRBuilder yet.", expr_idx);
        return false;
    }

    IRRegId dst = ctx.func->allocateVirtualRegister();
    emitInst(ctx, makeSimpleInst(inst_kind, IRValue::makeVirtualRegisterValue(dst),
                                 IRValue::makeVirtualRegisterValue(operand_reg)));
    *out_reg = dst;
    return true;
}

bool IRBuilder::buildCallExpr(FuncBuildCtx &ctx, ASTNodeIndex expr_idx, IRRegId *out_reg) {
    const ASTNode &expr = unit_->pool.getNode(expr_idx);
    const CallExprPayload &call_payload = expr.payload.call;

    IRRegId callee_reg = 0;
    if (!buildExpr(ctx, call_payload.callee, &callee_reg)) {
        return false;
    }

    auto arg_nodes = unit_->pool.get_list(call_payload.arguments);
    std::vector<IRRegId> arg_value_regs;
    arg_value_regs.reserve(arg_nodes.size());

    for (ASTNodeIndex arg_idx : arg_nodes) {
        IRRegId arg_value_reg = 0;
        if (!buildExpr(ctx, arg_idx, &arg_value_reg)) {
            return false;
        }
        arg_value_regs.push_back(arg_value_reg);
    }

    // Call 约定: second_operand 指向参数窗口起始寄存器，auxiliary_data 是参数个数。
    // 为保证 lowering/VM 可按连续窗口读取参数，这里先收集值，再连续分配参数槽位。
    std::vector<IRRegId> arg_slot_regs;
    arg_slot_regs.reserve(arg_value_regs.size());
    for (size_t idx = 0; idx < arg_value_regs.size(); ++idx) {
        (void)idx;
        arg_slot_regs.push_back(ctx.func->allocateVirtualRegister());
    }
    for (size_t idx = 0; idx < arg_value_regs.size(); ++idx) {
        emitInst(ctx, makeSimpleInst(IRInstKind::Move, IRValue::makeVirtualRegisterValue(arg_slot_regs[idx]),
                                     IRValue::makeVirtualRegisterValue(arg_value_regs[idx])));
    }

    IRRegId dst = ctx.func->allocateVirtualRegister();
    IRValue arg_base = IRValue::makeInvalid();
    if (!arg_slot_regs.empty()) {
        arg_base = IRValue::makeVirtualRegisterValue(arg_slot_regs.front());
    }

    emitInst(ctx, makeSimpleInst(IRInstKind::Call, IRValue::makeVirtualRegisterValue(dst),
                                 IRValue::makeVirtualRegisterValue(callee_reg), arg_base, IRValue::makeInvalid(),
                                 static_cast<uint32_t>(arg_slot_regs.size())));
    *out_reg = dst;
    return true;
}

bool IRBuilder::buildArrayExpr(FuncBuildCtx &ctx, ASTNodeIndex expr_idx, IRRegId *out_reg) {
    const ASTNode &expr = unit_->pool.getNode(expr_idx);
    auto elements = unit_->pool.get_list(expr.payload.list.elements);

    IRRegId dst = ctx.func->allocateVirtualRegister();
    emitInst(ctx,
             makeSimpleInst(IRInstKind::NewArray, IRValue::makeVirtualRegisterValue(dst), IRValue::makeInvalid(),
                            IRValue::makeInvalid(), IRValue::makeInvalid(), static_cast<uint32_t>(elements.size())));

    for (ASTNodeIndex element_idx : elements) {
        IRRegId element_reg = 0;
        if (!buildExpr(ctx, element_idx, &element_reg)) {
            return false;
        }
        emitInst(ctx, makeSimpleInst(IRInstKind::PushArray, IRValue::makeInvalid(),
                                     IRValue::makeVirtualRegisterValue(dst),
                                     IRValue::makeVirtualRegisterValue(element_reg)));
    }

    *out_reg = dst;
    return true;
}

bool IRBuilder::buildMapExpr(FuncBuildCtx &ctx, ASTNodeIndex expr_idx, IRRegId *out_reg) {
    const ASTNode &expr = unit_->pool.getNode(expr_idx);
    uint32_t map_idx = expr.payload.map.map_data_index;
    if (map_idx >= unit_->pool.map_data.size()) {
        reportError("Map expression payload index out of range.", expr_idx);
        return false;
    }

    const MapData &map_data = unit_->pool.map_data[map_idx];
    auto keys = unit_->pool.get_list(map_data.keys);
    auto values = unit_->pool.get_list(map_data.values);
    if (keys.size() != values.size()) {
        reportError("Map literal keys/values size mismatch.", expr_idx);
        return false;
    }

    IRRegId dst = ctx.func->allocateVirtualRegister();
    emitInst(ctx, makeSimpleInst(IRInstKind::NewMap, IRValue::makeVirtualRegisterValue(dst), IRValue::makeInvalid(),
                                 IRValue::makeInvalid(), IRValue::makeInvalid(), static_cast<uint32_t>(keys.size())));

    for (size_t idx = 0; idx < keys.size(); ++idx) {
        IRRegId key_reg = 0;
        IRRegId value_reg = 0;
        if (!buildExpr(ctx, keys[idx], &key_reg) || !buildExpr(ctx, values[idx], &value_reg)) {
            return false;
        }
        emitInst(ctx, makeSimpleInst(IRInstKind::SetMap, IRValue::makeInvalid(),
                                     IRValue::makeVirtualRegisterValue(dst), IRValue::makeVirtualRegisterValue(key_reg),
                                     IRValue::makeVirtualRegisterValue(value_reg)));
    }

    *out_reg = dst;
    return true;
}

bool IRBuilder::buildIndexExpr(FuncBuildCtx &ctx, ASTNodeIndex expr_idx, IRRegId *out_reg) {
    const ASTNode &expr = unit_->pool.getNode(expr_idx);

    IRRegId target_reg = 0;
    IRRegId index_reg = 0;
    if (!buildExpr(ctx, expr.payload.index.target, &target_reg) || !buildExpr(ctx, expr.payload.index.index, &index_reg)) {
        return false;
    }

    IRRegId dst = ctx.func->allocateVirtualRegister();
    emitInst(ctx, makeSimpleInst(IRInstKind::GetIndex, IRValue::makeVirtualRegisterValue(dst),
                                 IRValue::makeVirtualRegisterValue(target_reg),
                                 IRValue::makeVirtualRegisterValue(index_reg)));
    *out_reg = dst;
    return true;
}

bool IRBuilder::buildMemberExpr(FuncBuildCtx &ctx, ASTNodeIndex expr_idx, IRRegId *out_reg) {
    const ASTNode &expr = unit_->pool.getNode(expr_idx);

    IRRegId object_reg = 0;
    if (!buildExpr(ctx, expr.payload.member.object, &object_reg)) {
        return false;
    }

    IRRegId dst = ctx.func->allocateVirtualRegister();
    emitInst(ctx, makeSimpleInst(IRInstKind::GetMember, IRValue::makeVirtualRegisterValue(dst),
                                 IRValue::makeVirtualRegisterValue(object_reg),
                                 IRValue::makeStringIdentifierValue(expr.payload.member.property_id)));
    *out_reg = dst;
    return true;
}

} // namespace niki::ir
