#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include "niki/l0_core/vm/object.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <bit>
#include <vector>

namespace niki::ir {
using namespace niki::syntax;

namespace {
InstKind mapBinaryTokenToInst(TokenType token_type) {
    switch (token_type) {
    case TokenType::SYM_PLUS:
        return InstKind::Add;
    case TokenType::SYM_MINUS:
        return InstKind::Sub;
    case TokenType::SYM_STAR:
        return InstKind::Mul;
    case TokenType::SYM_SLASH:
        return InstKind::Div;
    case TokenType::SYM_MOD:
        return InstKind::Mod;
    case TokenType::SYM_EQUAL_EQUAL:
        return InstKind::CmpEq;
    case TokenType::SYM_BANG_EQUAL:
        return InstKind::CmpNe;
    case TokenType::SYM_LESS:
        return InstKind::CmpLt;
    case TokenType::SYM_LESS_EQUAL:
        return InstKind::CmpLe;
    case TokenType::SYM_GREATER:
        return InstKind::CmpGt;
    case TokenType::SYM_GREATER_EQUAL:
        return InstKind::CmpGe;
    case TokenType::SYM_AND:
        return InstKind::LogicAnd;
    case TokenType::SYM_OR:
        return InstKind::LogicOr;
    default:
        return InstKind::Nop;
    }
}
} // namespace

//------------------------------------------------------------------------------
// EXPR_ENTRY: 表达式降解入口，按表达式节点类型分发。
//------------------------------------------------------------------------------
bool IRBuilder::buildExpr(BuildCtx &bc, FuncCtx &fc, ASTNodeIndex expr_idx, RegId *out_reg) {
    if (!expr_idx.isvalid()) {
        error(bc, "Invalid expression index.", expr_idx);
        return false;
    }
    const ASTNode &expr = bc.unit->pool.getNode(expr_idx);
    switch (expr.type) {
    case NodeType::LiteralExpr: {
        // LITERAL: 字面量物化到新寄存器。
        uint32_t const_pool_index = expr.payload.literal.const_pool_index;
        if (const_pool_index >= bc.unit->pool.constants.size()) {
            error(bc, "Literal const pool index out of range.", expr_idx);
            return false;
        }
        const vm::Value &literal = bc.unit->pool.constants[const_pool_index];
        const RegId destination_reg = allocVReg(bc, fc);
        switch (literal.type) {
        case vm::ValueType::Integer:
            emitConstantI64(bc, fc, destination_reg, literal.as.integer);
            break;
        case vm::ValueType::Bool:
            emitConstantBool(bc, fc, destination_reg, literal.as.boolean);
            break;
        case vm::ValueType::Float: {
            const uint64_t float_bits = std::bit_cast<uint64_t>(literal.as.floating);
            emitConstantF64Bits(bc, fc, destination_reg, float_bits);
            break;
        }
        case vm::ValueType::Object: {
            vm::Object *obj = static_cast<vm::Object *>(literal.as.object);
            if (obj == nullptr || obj->type != vm::ObjType::String) {
                error(bc, "Only string object literal is supported by current IR builder.", expr_idx);
                return false;
            }
            const vm::ObjString *str_obj = static_cast<const vm::ObjString *>(literal.as.object);
            const uint32_t string_id = bc.module.intern(std::string(str_obj->chars, str_obj->length));
            emitConstantStringId(bc, fc, destination_reg, string_id);
            break;
        }
        default:
            error(bc, "Literal type is not supported by current IR builder.", expr_idx);
            return false;
        }
        *out_reg = destination_reg;
        return true;
    }
    case NodeType::IdentifierExpr: {
        // IDENT: 当前阶段仅在函数局部寄存器映射中解析标识符。
        const uint32_t name_sid = expr.payload.identifier.name_id;
        auto it = fc.local_vreg_by_name_sid.find(name_sid);
        if (it != fc.local_vreg_by_name_sid.end()) {
            *out_reg = it->second;
            return true;
        }
        error(bc, "Identifier is unresolved in current function scope.", expr_idx);
        return false;
    }
    case NodeType::BinaryExpr:
    case NodeType::LogicalExpr: {
        // BINARY: 二元/逻辑表达式统一降解为二元算术指令形态。
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
        RegId left_reg = 0;
        RegId right_reg = 0;
        if (!buildExpr(bc, fc, left_idx, &left_reg) || !buildExpr(bc, fc, right_idx, &right_reg)) {
            return false;
        }
        const InstKind inst_kind = mapBinaryTokenToInst(op);
        if (inst_kind == InstKind::Nop) {
            error(bc, "Unsupported binary operator.", expr_idx);
            return false;
        }
        const RegId destination_reg = allocVReg(bc, fc);
        emitBinaryReg(bc, fc, inst_kind, destination_reg, left_reg, right_reg);
        *out_reg = destination_reg;
        return true;
    }
    case NodeType::UnaryExpr: {
        // UNARY: 一元+降解为 Move，一元-和!降解为专用 opcode。
        RegId operand_reg = 0;
        if (!buildExpr(bc, fc, expr.payload.unary.operand, &operand_reg)) {
            return false;
        }
        const RegId destination_reg = allocVReg(bc, fc);
        if (expr.payload.unary.op == TokenType::SYM_PLUS) {
            emitMoveRegToReg(bc, fc, destination_reg, operand_reg);
        } else if (expr.payload.unary.op == TokenType::SYM_MINUS) {
            emitUnaryReg(bc, fc, InstKind::Neg, destination_reg, operand_reg);
        } else if (expr.payload.unary.op == TokenType::SYM_BANG) {
            emitUnaryReg(bc, fc, InstKind::LogicNot, destination_reg, operand_reg);
        } else {
            error(bc, "Unsupported unary operator.", expr_idx);
            return false;
        }
        *out_reg = destination_reg;
        return true;
    }
    case NodeType::CallExpr: {
        // CALL: 调用约定降解。
        // STEP: 先求值 callee 与每个参数表达式。
        // STEP: 参数值搬运到连续寄存器窗口。
        // STEP: 参数个数写入 auxiliary_data。
        const auto &payload = expr.payload.call;
        RegId callee_reg = 0;
        if (!buildExpr(bc, fc, payload.callee, &callee_reg)) {
            return false;
        }
        auto argument_nodes = bc.unit->pool.get_list(payload.arguments);
        std::vector<RegId> argument_value_regs;
        argument_value_regs.reserve(argument_nodes.size());
        for (ASTNodeIndex argument_node_idx : argument_nodes) {
            RegId argument_value_reg = 0;
            if (!buildExpr(bc, fc, argument_node_idx, &argument_value_reg)) {
                return false;
            }
            argument_value_regs.push_back(argument_value_reg);
        }
        std::vector<RegId> argument_slot_regs;
        argument_slot_regs.reserve(argument_value_regs.size());
        for (size_t argument_index = 0; argument_index < argument_value_regs.size(); ++argument_index) {
            (void)argument_index;
            argument_slot_regs.push_back(allocVReg(bc, fc));
        }
        for (size_t argument_index = 0; argument_index < argument_value_regs.size(); ++argument_index) {
            emitMoveRegToReg(bc, fc, argument_slot_regs[argument_index], argument_value_regs[argument_index]);
        }
        const RegId destination_reg = allocVReg(bc, fc);
        emit(bc, fc, InstKind::Call, ValueKind::VReg, destination_reg, 0, 0, ValueKind::VReg, callee_reg, 0, 0,
             argument_slot_regs.empty() ? ValueKind::Invalid : ValueKind::VReg,
             argument_slot_regs.empty() ? 0u : argument_slot_regs.front(), 0, 0, ValueKind::Invalid, 0, 0, 0,
             static_cast<uint32_t>(argument_slot_regs.size()));
        *out_reg = destination_reg;
        return true;
    }
    case NodeType::ArrayExpr: {
        // ARRAY: 聚合构造降解为 NewArray + PushArray*。
        auto elements = bc.unit->pool.get_list(expr.payload.list.elements);
        const RegId destination_reg = allocVReg(bc, fc);
        emit(bc, fc, InstKind::NewArray, ValueKind::VReg, destination_reg, 0, 0, ValueKind::Invalid, 0, 0, 0,
             ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0, static_cast<uint32_t>(elements.size()));
        for (ASTNodeIndex element_idx : elements) {
            RegId element_reg = 0;
            if (!buildExpr(bc, fc, element_idx, &element_reg)) {
                return false;
            }
            emit(bc, fc, InstKind::PushArray, ValueKind::Invalid, 0, 0, 0, ValueKind::VReg, destination_reg, 0, 0,
                 ValueKind::VReg, element_reg, 0, 0, ValueKind::Invalid, 0, 0, 0);
        }
        *out_reg = destination_reg;
        return true;
    }
    case NodeType::MapExpr: {
        // MAP: 聚合构造降解为 NewMap + SetMap*。
        const uint32_t map_idx = expr.payload.map.map_data_index;
        if (map_idx >= bc.unit->pool.map_data.size()) {
            error(bc, "Map expression payload index out of range.", expr_idx);
            return false;
        }
        const MapData &map_data = bc.unit->pool.map_data[map_idx];
        auto keys = bc.unit->pool.get_list(map_data.keys);
        auto values = bc.unit->pool.get_list(map_data.values);
        if (keys.size() != values.size()) {
            error(bc, "Map literal keys/values size mismatch.", expr_idx);
            return false;
        }
        const RegId destination_reg = allocVReg(bc, fc);
        emit(bc, fc, InstKind::NewMap, ValueKind::VReg, destination_reg, 0, 0, ValueKind::Invalid, 0, 0, 0,
             ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0, static_cast<uint32_t>(keys.size()));
        for (size_t pair_index = 0; pair_index < keys.size(); ++pair_index) {
            RegId key_reg = 0;
            RegId value_reg = 0;
            if (!buildExpr(bc, fc, keys[pair_index], &key_reg) || !buildExpr(bc, fc, values[pair_index], &value_reg)) {
                return false;
            }
            emit(bc, fc, InstKind::SetMap, ValueKind::Invalid, 0, 0, 0, ValueKind::VReg, destination_reg, 0, 0,
                 ValueKind::VReg, key_reg, 0, 0, ValueKind::VReg, value_reg, 0, 0);
        }
        *out_reg = destination_reg;
        return true;
    }
    case NodeType::IndexExpr: {
        // INDEX_GET: 索引读取降解为 GetIndex(target, index) -> dst。
        RegId target = 0;
        RegId index = 0;
        if (!buildExpr(bc, fc, expr.payload.index.target, &target) ||
            !buildExpr(bc, fc, expr.payload.index.index, &index)) {
            return false;
        }
        const RegId destination_reg = allocVReg(bc, fc);
        emit(bc, fc, InstKind::GetIndex, ValueKind::VReg, destination_reg, 0, 0, ValueKind::VReg, target, 0, 0,
             ValueKind::VReg, index, 0, 0, ValueKind::Invalid, 0, 0, 0);
        *out_reg = destination_reg;
        return true;
    }
    case NodeType::MemberExpr: {
        // MEMBER_GET: 成员读取降解为 GetMember(object, property_id) -> dst。
        RegId object = 0;
        if (!buildExpr(bc, fc, expr.payload.member.object, &object)) {
            return false;
        }
        const RegId destination_reg = allocVReg(bc, fc);
        emit(bc, fc, InstKind::GetMember, ValueKind::VReg, destination_reg, 0, 0, ValueKind::VReg, object, 0, 0,
             ValueKind::StringId, expr.payload.member.property_id, 0, 0, ValueKind::Invalid, 0, 0, 0);
        *out_reg = destination_reg;
        return true;
    }
    default:
        error(bc, "Expression node is not supported by current flat IR builder.", expr_idx);
        return false;
    }
}

} // namespace niki::ir
