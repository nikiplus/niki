#include "niki/l0_core/ir/lower_to_chunk.hpp"
#include "niki/l0_core/vm/chunk.hpp"
#include "niki/l0_core/vm/opcode.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <bit>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>
namespace niki::ir {
namespace {
//------------------------------------------------------------------------------
// WRITER: 字节码写入辅助。
//------------------------------------------------------------------------------
struct BytecodeWriter {
    niki::Chunk chunk;
    void writeByte(uint8_t byte, uint32_t source_line = 0, uint32_t source_col = 0) {
        chunk.code.push_back(byte);
        chunk.lines.push_back(source_line);
        chunk.columns.push_back(source_col);
    }
    void writeOp(vm::OPCODE op, uint32_t source_line = 0, uint32_t source_col = 0) {
        writeByte(vm::ToInt(op), source_line, source_col);
    }
    // IMM16: 大端写入 16-bit 立即数，和 VM::tryReadShort 对齐。
    void writeU16(uint16_t value, uint32_t source_line = 0, uint32_t source_col = 0) {
        writeByte(static_cast<uint8_t>((value >> 8) & 0xFF), source_line, source_col);
        writeByte(static_cast<uint8_t>(value & 0xFF), source_line, source_col);
    }
    // PATCH16: 回填某个 offset 的 16-bit 立即数（大端）。
    void patchU16(size_t code_offset, uint16_t value) {
        chunk.code[code_offset] = static_cast<uint8_t>((value >> 8) & 0xFF);
        chunk.code[code_offset + 1] = static_cast<uint8_t>(value & 0xFF);
    }
    uint16_t addConstant(vm::Value value) {
        const uint16_t constant_index = static_cast<uint16_t>(chunk.constants.size());
        chunk.constants.push_back(value);
        return constant_index;
    }
};
//------------------------------------------------------------------------------
// VALUE: IR 常量值到 VM 常量池值映射。
//------------------------------------------------------------------------------
std::expected<vm::Value, std::string> lowerImmediateToValue(ValueKind value_kind, uint32_t u32_payload,
                                                            int64_t i64_payload, uint64_t u64_payload,
                                                            const ModuleIR &module_ir) {
    switch (value_kind) {
    case ValueKind::ImmI64:
        return vm::Value::makeInt(i64_payload);
    case ValueKind::ImmBool:
        return vm::Value::makeBool(u32_payload != 0u);
    case ValueKind::ImmF64Bits: {
        double f = std::bit_cast<double>(u64_payload);
        return vm::Value::makeFloat(f);
    }
    case ValueKind::StringId: {
        if (u32_payload >= module_ir.string_pool.size()) {
            return std::unexpected("StringId out of range during lowering.");
        }
        const std::string &text = module_ir.string_pool[u32_payload];
        vm::ObjString *string_object = vm::allocateString(text.c_str(), static_cast<uint32_t>(text.size()));
        return vm::Value::makeObject(string_object);
    }
    default:
        return std::unexpected("Unsupported immediate kind for Constant lowering.");
    }
}
//------------------------------------------------------------------------------
// EMIT: 常用发射封装（减少样板）。
//------------------------------------------------------------------------------
void emitLoadConst(BytecodeWriter &writer, uint8_t dst_reg, uint16_t constant_index, uint32_t source_line,
                   uint32_t source_col) {
    if (constant_index <= std::numeric_limits<uint8_t>::max()) {
        writer.writeOp(vm::OPCODE::OP_LOAD_CONST, source_line, source_col);
        writer.writeByte(dst_reg, source_line, source_col);
        writer.writeByte(static_cast<uint8_t>(constant_index), source_line, source_col);
    } else {
        writer.writeOp(vm::OPCODE::OP_LOAD_CONST_W, source_line, source_col);
        writer.writeByte(dst_reg, source_line, source_col);
        writer.writeU16(constant_index, source_line, source_col);
    }
}
void emitMove(BytecodeWriter &writer, uint8_t dst_reg, uint8_t src_reg, uint32_t source_line, uint32_t source_col) {
    writer.writeOp(vm::OPCODE::OP_MOVE, source_line, source_col);
    writer.writeByte(dst_reg, source_line, source_col);
    writer.writeByte(src_reg, source_line, source_col);
}
bool requireVReg(ValueKind value_kind, uint32_t payload, uint8_t *out_reg) {
    if (value_kind != ValueKind::VReg || payload > std::numeric_limits<uint8_t>::max()) {
        return false;
    }
    *out_reg = static_cast<uint8_t>(payload);
    return true;
}
std::expected<vm::OPCODE, std::string> mapBinaryOpcode(InstKind inst_kind) {
    switch (inst_kind) {
    case InstKind::Add:
        return vm::OPCODE::OP_IADD;
    case InstKind::Sub:
        return vm::OPCODE::OP_ISUB;
    case InstKind::Mul:
        return vm::OPCODE::OP_IMUL;
    case InstKind::Div:
        return vm::OPCODE::OP_IDIV;
    case InstKind::Mod:
        return vm::OPCODE::OP_IMOD;
    case InstKind::CmpEq:
        return vm::OPCODE::OP_IEQ;
    case InstKind::CmpNe:
        return vm::OPCODE::OP_INE;
    case InstKind::CmpLt:
        return vm::OPCODE::OP_ILT;
    case InstKind::CmpLe:
        return vm::OPCODE::OP_ILE;
    case InstKind::CmpGt:
        return vm::OPCODE::OP_IGT;
    case InstKind::CmpGe:
        return vm::OPCODE::OP_IGE;
    case InstKind::LogicAnd:
        return vm::OPCODE::OP_AND;
    case InstKind::LogicOr:
        return vm::OPCODE::OP_OR;
    default:
        return std::unexpected("InstKind has no direct VM binary opcode mapping.");
    }
}
//------------------------------------------------------------------------------
// PATCH: 分支/跳转回填记录。
//------------------------------------------------------------------------------
struct PendingJumpPatch {
    size_t offset_u16_code_index = 0; // 指向 16-bit offset 的首字节位置
    BlockId target_block_id = std::numeric_limits<BlockId>::max();
    size_t offset_base_code_index = 0; // 相对偏移基准（通常是该指令末尾）
};
// FUNC: 降解单函数主体。
std::expected<vm::ObjFunction *, std::string> lowerOneFunction(const ModuleIR &module_ir,
                                                               const FuncRecord &function_record) {
    if (function_record.block_span.begin + function_record.block_span.count > module_ir.blocks.size()) {
        return std::unexpected("Function block span out of range.");
    }
    auto *function_object = new vm::ObjFunction();
    function_object->object_header.type = vm::ObjType::Function;
    function_object->object_header.isMarked = false;
    function_object->name_id = function_record.func_name_sid;
    function_object->arity = 0; // TODO: 后续接入真实形参计数
    function_object->max_registers = static_cast<uint16_t>(function_record.next_vreg);
    BytecodeWriter writer;
    writer.chunk.string_pool = module_ir.string_pool;
    writer.chunk.max_register_slots = static_cast<uint16_t>(function_record.next_vreg);
    std::unordered_map<BlockId, size_t> block_entry_code_index;
    std::vector<PendingJumpPatch> pending_patches;
    // BLOCK_PASS: 顺序展开 block，先记录入口 code offset。
    for (uint32_t relative_block_index = 0; relative_block_index < function_record.block_span.count;
         ++relative_block_index) {
        const uint32_t absolute_block_index = function_record.block_span.begin + relative_block_index;
        const BlockRecord &block_record = module_ir.blocks[absolute_block_index];
        block_entry_code_index[block_record.block_id] = writer.chunk.code.size();
        const uint32_t instruction_begin = block_record.inst_span.begin;
        const uint32_t instruction_end = instruction_begin + block_record.inst_span.count;
        for (uint32_t instruction_index = instruction_begin; instruction_index < instruction_end; ++instruction_index) {
            const InstKind inst_kind = module_ir.insts.kind[instruction_index];
            const ValueKind dst_kind = module_ir.insts.dst_kind[instruction_index];
            const uint32_t dst_u32 = module_ir.insts.dst_u32[instruction_index];
            const ValueKind first_kind = module_ir.insts.a_kind[instruction_index];
            const uint32_t first_u32 = module_ir.insts.a_u32[instruction_index];
            const int64_t first_i64 = module_ir.insts.a_i64[instruction_index];
            const uint64_t first_u64 = module_ir.insts.a_u64[instruction_index];
            const ValueKind second_kind = module_ir.insts.b_kind[instruction_index];
            const uint32_t second_u32 = module_ir.insts.b_u32[instruction_index];
            const ValueKind third_kind = module_ir.insts.c_kind[instruction_index];
            const uint32_t third_u32 = module_ir.insts.c_u32[instruction_index];
            const uint32_t auxiliary_data = module_ir.insts.aux[instruction_index];
            const uint32_t source_line = module_ir.insts.src_line[instruction_index];
            const uint32_t source_col = module_ir.insts.src_col[instruction_index];
            switch (inst_kind) {
            case InstKind::Nop:
                break;
            case InstKind::Constant: {
                uint8_t dst_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &dst_reg)) {
                    return std::unexpected("Constant expects dst VReg <= 255.");
                }
                auto immediate_value = lowerImmediateToValue(first_kind, first_u32, first_i64, first_u64, module_ir);
                if (!immediate_value.has_value()) {
                    return std::unexpected(immediate_value.error());
                }
                const uint16_t constant_index = writer.addConstant(immediate_value.value());
                emitLoadConst(writer, dst_reg, constant_index, source_line, source_col);
                break;
            }
            case InstKind::Move: {
                uint8_t dst_reg = 0;
                uint8_t src_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &dst_reg) || !requireVReg(first_kind, first_u32, &src_reg)) {
                    return std::unexpected("Move expects dst/src VReg <= 255.");
                }
                emitMove(writer, dst_reg, src_reg, source_line, source_col);
                break;
            }
            case InstKind::Add:
            case InstKind::Sub:
            case InstKind::Mul:
            case InstKind::Div:
            case InstKind::Mod:
            case InstKind::CmpEq:
            case InstKind::CmpNe:
            case InstKind::CmpLt:
            case InstKind::CmpLe:
            case InstKind::CmpGt:
            case InstKind::CmpGe:
            case InstKind::LogicAnd:
            case InstKind::LogicOr: {
                auto opcode = mapBinaryOpcode(inst_kind);
                if (!opcode.has_value()) {
                    return std::unexpected(opcode.error());
                }
                uint8_t dst_reg = 0;
                uint8_t left_reg = 0;
                uint8_t right_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &dst_reg) || !requireVReg(first_kind, first_u32, &left_reg) ||
                    !requireVReg(second_kind, second_u32, &right_reg)) {
                    return std::unexpected("Binary instruction expects VReg operands <= 255.");
                }
                writer.writeOp(opcode.value(), source_line, source_col);
                writer.writeByte(dst_reg, source_line, source_col);
                writer.writeByte(left_reg, source_line, source_col);
                writer.writeByte(right_reg, source_line, source_col);
                break;
            }
            case InstKind::Neg: {
                uint8_t dst_reg = 0;
                uint8_t src_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &dst_reg) || !requireVReg(first_kind, first_u32, &src_reg)) {
                    return std::unexpected("Neg expects dst/src VReg <= 255.");
                }
                writer.writeOp(vm::OPCODE::OP_NEG, source_line, source_col);
                writer.writeByte(dst_reg, source_line, source_col);
                writer.writeByte(src_reg, source_line, source_col);
                break;
            }
            case InstKind::LogicNot: {
                uint8_t dst_reg = 0;
                uint8_t src_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &dst_reg) || !requireVReg(first_kind, first_u32, &src_reg)) {
                    return std::unexpected("LogicNot expects dst/src VReg <= 255.");
                }
                writer.writeOp(vm::OPCODE::OP_NOT, source_line, source_col);
                writer.writeByte(dst_reg, source_line, source_col);
                writer.writeByte(src_reg, source_line, source_col);
                break;
            }
            case InstKind::NewArray: {
                uint8_t dst_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &dst_reg)) {
                    return std::unexpected("NewArray expects dst VReg <= 255.");
                }
                writer.writeOp(vm::OPCODE::OP_NEW_ARRAY, source_line, source_col);
                writer.writeByte(dst_reg, source_line, source_col);
                writer.writeByte(static_cast<uint8_t>(auxiliary_data & 0xFF), source_line, source_col);
                break;
            }
            case InstKind::PushArray: {
                uint8_t array_reg = 0;
                uint8_t value_reg = 0;
                if (!requireVReg(first_kind, first_u32, &array_reg) ||
                    !requireVReg(second_kind, second_u32, &value_reg)) {
                    return std::unexpected("PushArray expects array/value VReg <= 255.");
                }
                writer.writeOp(vm::OPCODE::OP_PUSH_ARRAY, source_line, source_col);
                writer.writeByte(array_reg, source_line, source_col);
                writer.writeByte(value_reg, source_line, source_col);
                break;
            }
            case InstKind::NewMap: {
                uint8_t dst_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &dst_reg)) {
                    return std::unexpected("NewMap expects dst VReg <= 255.");
                }
                writer.writeOp(vm::OPCODE::OP_NEW_MAP, source_line, source_col);
                writer.writeByte(dst_reg, source_line, source_col);
                writer.writeByte(static_cast<uint8_t>(auxiliary_data & 0xFF), source_line, source_col);
                break;
            }
            case InstKind::SetMap: {
                uint8_t map_reg = 0;
                uint8_t key_reg = 0;
                uint8_t value_reg = 0;
                if (!requireVReg(first_kind, first_u32, &map_reg) || !requireVReg(second_kind, second_u32, &key_reg) ||
                    !requireVReg(third_kind, third_u32, &value_reg)) {
                    return std::unexpected("SetMap expects map/key/value VReg <= 255.");
                }
                writer.writeOp(vm::OPCODE::OP_SET_MAP, source_line, source_col);
                writer.writeByte(map_reg, source_line, source_col);
                writer.writeByte(key_reg, source_line, source_col);
                writer.writeByte(value_reg, source_line, source_col);
                break;
            }
            case InstKind::GetIndex: {
                uint8_t dst_reg = 0;
                uint8_t array_reg = 0;
                uint8_t index_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &dst_reg) || !requireVReg(first_kind, first_u32, &array_reg) ||
                    !requireVReg(second_kind, second_u32, &index_reg)) {
                    return std::unexpected("GetIndex expects dst/array/index VReg <= 255.");
                }
                writer.writeOp(vm::OPCODE::OP_GET_ARRAY, source_line, source_col);
                writer.writeByte(dst_reg, source_line, source_col);
                writer.writeByte(array_reg, source_line, source_col);
                writer.writeByte(index_reg, source_line, source_col);
                break;
            }
            case InstKind::SetIndex: {
                uint8_t array_reg = 0;
                uint8_t index_reg = 0;
                uint8_t value_reg = 0;
                if (!requireVReg(first_kind, first_u32, &array_reg) ||
                    !requireVReg(second_kind, second_u32, &index_reg) ||
                    !requireVReg(third_kind, third_u32, &value_reg)) {
                    return std::unexpected("SetIndex expects array/index/value VReg <= 255.");
                }
                writer.writeOp(vm::OPCODE::OP_SET_ARRAY, source_line, source_col);
                writer.writeByte(array_reg, source_line, source_col);
                writer.writeByte(index_reg, source_line, source_col);
                writer.writeByte(value_reg, source_line, source_col);
                break;
            }
            case InstKind::Call: {
                uint8_t out_reg = 0;
                uint8_t callee_reg = 0;
                uint8_t arg_start_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &out_reg)) {
                    return std::unexpected("Call expects dst VReg <= 255.");
                }
                if (!requireVReg(first_kind, first_u32, &callee_reg)) {
                    return std::unexpected("Call currently expects callee as VReg.");
                }
                if (second_kind == ValueKind::Invalid) {
                    arg_start_reg = 0;
                } else if (!requireVReg(second_kind, second_u32, &arg_start_reg)) {
                    return std::unexpected("Call arg base must be VReg <= 255.");
                }
                writer.writeOp(vm::OPCODE::OP_CALL, source_line, source_col);
                writer.writeByte(out_reg, source_line, source_col);
                writer.writeByte(callee_reg, source_line, source_col);
                writer.writeByte(arg_start_reg, source_line, source_col);
                writer.writeByte(static_cast<uint8_t>(auxiliary_data & 0xFF), source_line, source_col);
                break;
            }
            case InstKind::Jump: {
                if (first_kind != ValueKind::BlockId) {
                    return std::unexpected("Jump expects BlockId target.");
                }
                writer.writeOp(vm::OPCODE::OP_JMP, source_line, source_col);
                const size_t offset_u16_code_index = writer.chunk.code.size();
                writer.writeU16(0, source_line, source_col);
                pending_patches.push_back(PendingJumpPatch{
                    .offset_u16_code_index = offset_u16_code_index,
                    .target_block_id = static_cast<BlockId>(first_u32),
                    .offset_base_code_index = writer.chunk.code.size(),
                });
                break;
            }
            case InstKind::Branch: {
                // BRANCH_LOWER: 采用 JNZ + JMP 两条指令表达二分支。
                if (first_kind != ValueKind::VReg || second_kind != ValueKind::BlockId ||
                    third_kind != ValueKind::BlockId) {
                    return std::unexpected("Branch expects cond(VReg), true(BlockId), false(BlockId).");
                }
                uint8_t cond_reg = 0;
                if (!requireVReg(first_kind, first_u32, &cond_reg)) {
                    return std::unexpected("Branch condition register out of 8-bit range.");
                }
                // JNZ cond, true_target
                writer.writeOp(vm::OPCODE::OP_JNZ, source_line, source_col);
                writer.writeByte(cond_reg, source_line, source_col);
                const size_t jnz_offset_u16_index = writer.chunk.code.size();
                writer.writeU16(0, source_line, source_col);
                pending_patches.push_back(PendingJumpPatch{
                    .offset_u16_code_index = jnz_offset_u16_index,
                    .target_block_id = static_cast<BlockId>(second_u32),
                    .offset_base_code_index = writer.chunk.code.size(),
                });
                // JMP false_target
                writer.writeOp(vm::OPCODE::OP_JMP, source_line, source_col);
                const size_t jmp_offset_u16_index = writer.chunk.code.size();
                writer.writeU16(0, source_line, source_col);
                pending_patches.push_back(PendingJumpPatch{
                    .offset_u16_code_index = jmp_offset_u16_index,
                    .target_block_id = static_cast<BlockId>(third_u32),
                    .offset_base_code_index = writer.chunk.code.size(),
                });
                break;
            }
            case InstKind::Return: {
                // RETURN_PROTO: VM 约定返回值放在逻辑寄存器 r0，然后执行 OP_RETURN。
                if (first_kind == ValueKind::VReg && first_u32 != 0u) {
                    uint8_t src_reg = 0;
                    if (!requireVReg(first_kind, first_u32, &src_reg)) {
                        return std::unexpected("Return register out of 8-bit range.");
                    }
                    emitMove(writer, 0u, src_reg, source_line, source_col);
                }
                writer.writeOp(vm::OPCODE::OP_RETURN, source_line, source_col);
                break;
            }
            default:
                return std::unexpected("Unsupported InstKind in MVP lowering.");
            }
        }
    }
    // PATCH_APPLY: 按目标 block 入口回填相对偏移。
    for (const PendingJumpPatch &patch : pending_patches) {
        auto target_iter = block_entry_code_index.find(patch.target_block_id);
        if (target_iter == block_entry_code_index.end()) {
            return std::unexpected("Jump target block id not found during patch.");
        }
        const size_t target_code_index = target_iter->second;
        if (target_code_index < patch.offset_base_code_index) {
            return std::unexpected("Backward jump is not supported by this MVP lowering yet.");
        }
        const size_t relative_offset = target_code_index - patch.offset_base_code_index;
        if (relative_offset > std::numeric_limits<uint16_t>::max()) {
            return std::unexpected("Jump offset exceeds uint16 range.");
        }
        writer.patchU16(patch.offset_u16_code_index, static_cast<uint16_t>(relative_offset));
    }
    function_object->chunk = std::move(writer.chunk);
    return function_object;
}
} // namespace
std::expected<vm::ObjFunction *, std::string> lowerFunctionToChunk(const ModuleIR &module_ir, FuncId function_id) {
    if (function_id >= module_ir.funcs.size()) {
        return std::unexpected("Function id out of range.");
    }
    return lowerOneFunction(module_ir, module_ir.funcs[function_id]);
}
std::expected<LowerResult, std::string> lowerModuleToChunk(const ModuleIR &module_ir) {
    LowerResult result;
    result.functions.reserve(module_ir.funcs.size());
    for (uint32_t function_index = 0; function_index < module_ir.funcs.size(); ++function_index) {
        auto lowered_function = lowerFunctionToChunk(module_ir, function_index);
        if (!lowered_function.has_value()) {
            return std::unexpected("lower function #" + std::to_string(function_index) +
                                   " failed: " + lowered_function.error());
        }
        result.functions.push_back(lowered_function.value());
    }
    return result;
}
} // namespace niki::ir
