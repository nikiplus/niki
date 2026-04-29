#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include "niki/l0_core/vm/object.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <bit>
#include <vector>

/** @builder_expr_impl: 表达式求值路径降解实现
 * 这个文件实现表达式层降解，核心任务是回答：
 * “一个表达式的结果值放在哪个寄存器里，以及为得到这个结果需要发射哪些 IR 指令”。
 *
 * 与语句层构建控制流不同，表达式层关注值流：
 * 字面量物化、标识符解析、运算符映射、调用参数搬运、聚合构造、索引与成员访问。
 * 每个分支都以 `out_reg` 作为统一出口，使上层语句逻辑只关心“拿到哪个寄存器”，而不关心中间细节。
 *
 * 该文件还承担了一部分名字可见性兜底逻辑（局部优先，其次导入/顶层），
 * 目的是在 IR 层保持最小可执行一致性，并将不可解析情况尽早诊断，而不是延迟到后端执行时报错。
 *
 * 字段流说明（核心）：
 * - 表达式节点 `expr_idx` -> `setEmitLocation`：每次降级先刷新源码位置信息。
 * - `buildExpr(...)` 递归子调用 -> 子结果寄存器：父表达式按寄存器组合发射新指令。
 * - 字面量常量池项 -> `emitConstant*`：把值物化到新分配的目的寄存器。
 * - 标识符名 sid -> 局部表 / 导入与顶层检查：解析失败立即诊断，解析成功产出寄存器。
 * - `*out_reg`：统一对外输出“本表达式结果寄存器”，供语句层继续消费。
 */
namespace niki::ir {
using namespace niki::syntax;

namespace {
/**
 * @brief 将二元/逻辑运算 token 映射到 IR 指令。
 * @param token_type 运算符 token 类型。
 * @return InstKind 映射成功返回对应指令，未知返回 InstKind::Nop。
 */
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

/**
 * @brief 判断名称是否属于可见的导入项或模块顶层实体。
 * @param unit 当前编译单元。
 * @param name_sid 名称字符串 id。
 * @return true 名称可见（函数/结构体/显式导入）。
 * @return false 名称不可见或模块根不合法。
 */
bool isImportedOrTopLevelName(const GlobalCompilationUnit &unit, uint32_t name_sid) {
    if (!unit.root.isvalid()) {
        return false;
    }
    const auto &root_node = unit.pool.getNode(unit.root);
    if (root_node.type != NodeType::ModuleDecl && root_node.type != NodeType::ProgramRoot) {
        return false;
    }
    const ASTNodeIndex body_idx =
        (root_node.type == NodeType::ModuleDecl) ? root_node.payload.module_decl.body : unit.root;
    const auto &body_node = unit.pool.getNode(body_idx);
    auto decls = unit.pool.get_list(body_node.payload.list.elements);
    for (const ASTNodeIndex decl_idx : decls) {
        if (!decl_idx.isvalid()) {
            continue;
        }
        const auto &decl = unit.pool.getNode(decl_idx);
        if (decl.type == NodeType::FunctionDecl) {
            const auto &func_data = unit.pool.function_data[decl.payload.func_decl.function_index];
            if (func_data.name_id == name_sid) {
                return true;
            }
        } else if (decl.type == NodeType::StructDecl) {
            const auto &struct_data = unit.pool.struct_data[decl.payload.struct_decl.struct_index];
            if (struct_data.name_id == name_sid) {
                return true;
            }
        } else if (decl.type == NodeType::ImportDecl) {
            const auto &import_decl = unit.pool.import_decl_data[decl.payload.import_decl.import_decl_index];
            if (import_decl.import_module_only) {
                continue;
            }
            for (uint32_t i = 0; i < import_decl.item_count; ++i) {
                const auto &item = unit.pool.import_items[import_decl.first_item_index + i];
                if (item.local_name_id == name_sid) {
                    return true;
                }
            }
        }
    }
    return false;
}
} // namespace

//------------------------------------------------------------------------------
// EXPR_ENTRY: 表达式降解入口，按表达式节点类型分发。
//------------------------------------------------------------------------------
/**
 * @brief 将表达式节点降级为 IR，并返回结果寄存器。
 * @param bc 构建上下文。
 * @param fc 函数上下文。
 * @param expr_idx 表达式节点索引。
 * @param out_reg 输出寄存器编号。
 * @return true 表达式降级成功并写入 out_reg。
 * @return false 节点不支持、常量/操作符非法或引用解析失败。
 */
bool IRBuilder::buildExpr(BuildCtx &bc, FuncCtx &fc, ASTNodeIndex expr_idx, RegId *out_reg) {
    if (!expr_idx.isvalid()) {
        error(bc, "Invalid expression index.", expr_idx);
        return false;
    }

    setEmitLocation(bc, fc, expr_idx);
    const ASTNode &expr = bc.unit->pool.getNode(expr_idx);

    // DISPATCH_RULE:
    // - 每个分支必须满足“成功时写出 *out_reg，失败时返回 false + 诊断”这一统一契约。
    // - 复杂表达式先递归求子表达式寄存器，再发射当前层指令。
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
        // IDENT: 先查函数局部；否则按全局符号加载（用于跨函数/跨模块引用）。
        const uint32_t name_sid = expr.payload.identifier.name_id;
        auto it = fc.local_vreg_by_name_sid.find(name_sid);
        if (it != fc.local_vreg_by_name_sid.end()) {
            *out_reg = it->second;
            return true;
        }
        if (!isImportedOrTopLevelName(*bc.unit, name_sid)) {
            error(bc, "Identifier is unresolved in current function scope.", expr_idx);
            return false;
        }
        const RegId destination_reg = allocVReg(bc, fc);
        emit(bc, fc, InstKind::LoadGlobal, ValueKind::VReg, destination_reg, 0, 0, ValueKind::ImmI64,
             static_cast<uint32_t>(name_sid), static_cast<int64_t>(name_sid), 0,
             ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0);
        *out_reg = destination_reg;
        return true;
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
        setEmitLocation(bc, fc, expr_idx);
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
        setEmitLocation(bc, fc, expr_idx);
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
        // 调用约定说明（仅说明一次）：
        // 1) 参数表达式先各自求值得到 value regs；
        // 2) 再搬运到连续参数槽位寄存器窗口；
        // 3) OP_CALL 记录起始槽位 + 参数个数，callee 按窗口读取。
        std::vector<RegId> argument_slot_regs;
        argument_slot_regs.reserve(argument_value_regs.size());
        for (size_t argument_index = 0; argument_index < argument_value_regs.size(); ++argument_index) {
            (void)argument_index;
            argument_slot_regs.push_back(allocVReg(bc, fc));
        }
        for (size_t argument_index = 0; argument_index < argument_value_regs.size(); ++argument_index) {
            setEmitLocation(bc, fc, expr_idx);
            emitMoveRegToReg(bc, fc, argument_slot_regs[argument_index], argument_value_regs[argument_index]);
        }
        setEmitLocation(bc, fc, expr_idx);
        const RegId destination_reg = allocVReg(bc, fc);
        emit(bc, fc, InstKind::Call, ValueKind::VReg, destination_reg, 0, 0, ValueKind::VReg, callee_reg, 0, 0,
             argument_slot_regs.empty() ? ValueKind::Invalid : ValueKind::VReg,
             argument_slot_regs.empty() ? 0u : argument_slot_regs.front(), 0, 0, ValueKind::Invalid, 0, 0, 0,
             static_cast<uint32_t>(argument_slot_regs.size()));
        *out_reg = destination_reg;
        return true;
    }

    case NodeType::ArrayExpr: {
        // ARRAY/MAP 采用同一模板：先创建容器，再逐元素（或键值对）追加写入。
        auto elements = bc.unit->pool.get_list(expr.payload.list.elements);
        const RegId destination_reg = allocVReg(bc, fc);
        setEmitLocation(bc, fc, expr_idx);
        emit(bc, fc, InstKind::NewArray, ValueKind::VReg, destination_reg, 0, 0, ValueKind::Invalid, 0, 0, 0,
             ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0, static_cast<uint32_t>(elements.size()));
        for (ASTNodeIndex element_idx : elements) {
            RegId element_reg = 0;
            if (!buildExpr(bc, fc, element_idx, &element_reg)) {
                return false;
            }
            setEmitLocation(bc, fc, expr_idx);
            emit(bc, fc, InstKind::PushArray, ValueKind::Invalid, 0, 0, 0, ValueKind::VReg, destination_reg, 0, 0,
                 ValueKind::VReg, element_reg, 0, 0, ValueKind::Invalid, 0, 0, 0);
        }
        *out_reg = destination_reg;
        return true;
    }

    case NodeType::MapExpr: {
        // 复用上方“先创建后填充”模板，这里只处理 map 的键值对版本。
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
        setEmitLocation(bc, fc, expr_idx);
        emit(bc, fc, InstKind::NewMap, ValueKind::VReg, destination_reg, 0, 0, ValueKind::Invalid, 0, 0, 0,
             ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0, static_cast<uint32_t>(keys.size()));
        for (size_t pair_index = 0; pair_index < keys.size(); ++pair_index) {
            RegId key_reg = 0;
            RegId value_reg = 0;
            if (!buildExpr(bc, fc, keys[pair_index], &key_reg) || !buildExpr(bc, fc, values[pair_index], &value_reg)) {
                return false;
            }
            setEmitLocation(bc, fc, expr_idx);
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
        setEmitLocation(bc, fc, expr_idx);
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
        setEmitLocation(bc, fc, expr_idx);
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
