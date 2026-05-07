#include "niki/l0_core/ir/lower_to_chunk.hpp"
#include "niki/l0_core/vm/chunk.hpp"
#include "niki/l0_core/vm/opcode.hpp"
#include "niki/l0_core/vm/opcode_capobility.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/** @lower_impl: IR 到 VM 字节码的降级实现
 * 这个文件实现 IR 后端编码器：把 `ModuleIR` 转换成 VM 可执行的 `ObjFunction/Chunk`。
 * 它承担的是“语义到编码”的最后桥接，不再讨论 AST/类型，只处理操作码、寄存器和跳转偏移。
 *
 * 实现上采用两阶段控制流编码策略：
 * - 第一遍按块顺序发射指令，并记录 jump/branch 的 patch 点；
 * - 第二遍根据目标块入口位置回填相对偏移，并处理前跳/回环跳方向。
 * 这是把逻辑块 id 转成字节码位移的必要算法步骤。
 *
 * 同时该文件集中维护 InstKind 到 opcode 的映射与可执行性检查，
 * 确保“IR 支持矩阵”与 VM 运行能力同步，不把未实现指令留到运行时才失败。
 *
 * 字段流说明（核心）：
 * - `ModuleIR.insts.*` -> 局部快照变量：每条 IR 指令先抽取 kind/payload，再进入 switch 编码。
 * - `RegId/ValueKind` -> opcode 操作数字节：通过 `requireVReg` 等守卫完成宽度与类型约束。
 * - `module_ir.string_pool` -> `writer.chunk.constants/string_pool`：常量与字符串在编码时落地到 VM 结构。
 * - `block_entry_code_index + pending_patches`：把块 id 级跳转目标转换为字节码相对偏移。
 * - `writer.chunk` -> `ObjFunction.chunk`：形成 runtime/VM 可直接消费的执行体。
 */
namespace niki::ir {
namespace {
//------------------------------------------------------------------------------
// WRITER: 字节码写入辅助。
//------------------------------------------------------------------------------
struct BytecodeWriter {
    niki::Chunk chunk;
    /// @brief 写入单字节并记录源码位置。
    void writeByte(uint8_t byte, uint32_t source_line = 0, uint32_t source_col = 0) {
        chunk.code.push_back(byte);
        chunk.lines.push_back(source_line);
        chunk.columns.push_back(source_col);
    }
    /// @brief 写入 opcode 字节。
    void writeOp(vm::OPCODE op, uint32_t source_line = 0, uint32_t source_col = 0) {
        writeByte(vm::ToInt(op), source_line, source_col);
    }
    /// @brief 写入可执行 opcode；若 VM 不支持则返回错误。
    std::expected<void, std::string> writeRunnableOp(vm::OPCODE op, uint32_t source_line = 0, uint32_t source_col = 0) {
        if (!vm::isRunnable(op)) {
            return std::unexpected("Lowering blocked: opcode not runnable in VM: " + std::string(vm::nameOf(op)));
        }
        writeOp(op, source_line, source_col);
        return {};
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
    /// @brief 追加常量并返回常量池下标。
    uint16_t addConstant(vm::Value value) {
        const uint16_t constant_index = static_cast<uint16_t>(chunk.constants.size());
        chunk.constants.push_back(value);
        return constant_index;
    }
};
//------------------------------------------------------------------------------
// VALUE: IR 常量值到 VM 常量池值映射。
//------------------------------------------------------------------------------
/**
 * @brief 将 IR 立即数槽位映射为 VM Value。
 * @param value_kind 立即数种类。
 * @param u32_payload 无符号载荷。
 * @param i64_payload 有符号载荷。
 * @param u64_payload 64 位载荷。
 * @param module_ir 模块 IR（用于字符串池访问）。
 * @return std::expected<vm::Value, std::string> 成功返回 Value，失败返回错误信息。
 */
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
/**
 * @brief 发射加载常量指令（自动选择窄/宽常量编码）。
 */
std::expected<void, std::string> emitLoadConst(BytecodeWriter &writer, uint8_t dst_reg, uint16_t constant_index,
                                               uint32_t source_line, uint32_t source_col) {
    if (constant_index <= std::numeric_limits<uint8_t>::max()) {
        if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_LOAD_CONST, source_line, source_col);
            !emitted.has_value()) {
            return std::unexpected(emitted.error());
        }
        writer.writeByte(dst_reg, source_line, source_col);
        writer.writeByte(static_cast<uint8_t>(constant_index), source_line, source_col);
    } else {
        if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_LOAD_CONST_W, source_line, source_col);
            !emitted.has_value()) {
            return std::unexpected(emitted.error());
        }
        writer.writeByte(dst_reg, source_line, source_col);
        writer.writeU16(constant_index, source_line, source_col);
    }
    return {};
}
/**
 * @brief 发射寄存器移动指令。
 */
std::expected<void, std::string> emitMove(BytecodeWriter &writer, uint8_t dst_reg, uint8_t src_reg,
                                          uint32_t source_line, uint32_t source_col) {
    if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_MOVE, source_line, source_col); !emitted.has_value()) {
        return std::unexpected(emitted.error());
    }
    writer.writeByte(dst_reg, source_line, source_col);
    writer.writeByte(src_reg, source_line, source_col);
    return {};
}
/**
 * @brief 校验并提取 8 位寄存器编号。
 * @return true 提取成功并写入 out_reg。
 * @return false 值类型非 VReg 或编号超出 8 位范围。
 */
bool requireVReg(ValueKind value_kind, uint32_t payload, uint8_t *out_reg) {
    if (value_kind != ValueKind::VReg || payload > std::numeric_limits<uint8_t>::max()) {
        return false;
    }
    *out_reg = static_cast<uint8_t>(payload);
    return true;
}
/**
 * @brief 将 IR 二元指令映射到 VM opcode。
 * @param inst_kind IR 指令种类。
 * @return std::expected<vm::OPCODE, std::string> 成功返回 opcode，失败返回错误信息。
 */
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

const std::vector<InstOpcodeChecklistEntry> kInstOpcodeChecklist = {
    {InstKind::Nop, std::nullopt, "No bytecode emission."},
    {InstKind::Constant, vm::OPCODE::OP_LOAD_CONST, "OP_LOAD_CONST/OP_LOAD_CONST_W selected by const index width."},
    {InstKind::Move, vm::OPCODE::OP_MOVE, "Direct register move."},
    {InstKind::Add, vm::OPCODE::OP_IADD, "Mapped by mapBinaryOpcode."},
    {InstKind::Sub, vm::OPCODE::OP_ISUB, "Mapped by mapBinaryOpcode."},
    {InstKind::Mul, vm::OPCODE::OP_IMUL, "Mapped by mapBinaryOpcode."},
    {InstKind::Div, vm::OPCODE::OP_IDIV, "Mapped by mapBinaryOpcode."},
    {InstKind::Mod, vm::OPCODE::OP_IMOD, "Mapped by mapBinaryOpcode."},
    {InstKind::Neg, vm::OPCODE::OP_NEG, "Unary negation."},
    {InstKind::CmpEq, vm::OPCODE::OP_IEQ, "Mapped by mapBinaryOpcode."},
    {InstKind::CmpNe, vm::OPCODE::OP_INE, "Mapped by mapBinaryOpcode."},
    {InstKind::CmpLt, vm::OPCODE::OP_ILT, "Mapped by mapBinaryOpcode."},
    {InstKind::CmpLe, vm::OPCODE::OP_ILE, "Mapped by mapBinaryOpcode."},
    {InstKind::CmpGt, vm::OPCODE::OP_IGT, "Mapped by mapBinaryOpcode."},
    {InstKind::CmpGe, vm::OPCODE::OP_IGE, "Mapped by mapBinaryOpcode."},
    {InstKind::LogicAnd, vm::OPCODE::OP_AND, "Mapped by mapBinaryOpcode."},
    {InstKind::LogicOr, vm::OPCODE::OP_OR, "Mapped by mapBinaryOpcode."},
    {InstKind::LogicNot, vm::OPCODE::OP_NOT, "Unary logic not."},
    {InstKind::LoadGlobal, vm::OPCODE::OP_GET_GLOBAL, "Top-level symbol lookup via name-id constant."},
    {InstKind::StoreGlobal, vm::OPCODE::OP_SET_GLOBAL, "Top-level symbol write via name-id constant."},
    {InstKind::Call, vm::OPCODE::OP_CALL, "Direct function call."},
    {InstKind::Return, vm::OPCODE::OP_RETURN, "Return protocol writes r0 then OP_RETURN."},
    {InstKind::NewArray, vm::OPCODE::OP_NEW_ARRAY, "Array create."},
    {InstKind::PushArray, vm::OPCODE::OP_PUSH_ARRAY, "Array push."},
    {InstKind::NewMap, vm::OPCODE::OP_NEW_MAP, "Map create."},
    {InstKind::SetMap, vm::OPCODE::OP_SET_MAP, "Map set."},
    {InstKind::GetIndex, vm::OPCODE::OP_GET_ARRAY, "Index load."},
    {InstKind::SetIndex, vm::OPCODE::OP_SET_ARRAY, "Index store."},
    {InstKind::GetMember, vm::OPCODE::OP_GET_PROPERTY, "Property read; blocked by opcode capability today."},
    {InstKind::SetMember, vm::OPCODE::OP_SET_PROPERTY, "Property write; blocked by opcode capability today."},
    {InstKind::Jump, vm::OPCODE::OP_JMP, "Forward jump; patched to OP_LOOP for backward edge."},
    {InstKind::Branch, vm::OPCODE::OP_JNZ, "Two-instruction lowering: JNZ + JMP/LOOP."},
    {InstKind::Phi, std::nullopt, "SSA phi not supported in MVP lowering."},
    {InstKind::Free, vm::OPCODE::OP_FREE, "Release heap object and set register to Nil."},
};
//------------------------------------------------------------------------------
// PATCH: 分支/跳转回填记录。
//------------------------------------------------------------------------------
/**
 * CHG-20260506 PendingJumpPatch 增加 original_opcode + opcode_off_back 字段。
 *
 * 旧实现仅有 offset_u16_code_index / target_block_id / offset_base_code_index 三个字段。
 * PATCH_APPLY 阶段对所有跳转指令统一覆盖 opcode 为 OP_JMP（或回边 OP_LOOP），
 * 且假设 opcode 总是在 u16 offset 前 1 字节处。
 *
 * 问题 1（漏盖 opcode）：Branch lower 生成了 JNZ + JMP 两条指令。回填时把 JNZ 的 opcode
 *                   字节覆写为 OP_JMP，条件分支退化为无条件跳转，VM 不再判断条件寄存器。
 * 问题 2（漏盖格式）：JNZ 编码为「opcode + cond_reg + u16_offset」，
 *                   opcode 在 u16 offset 前 2 字节，旧逻辑按 1 字节偏移覆盖，
 *                   实际改动了 cond_reg 字节而非 opcode 字节，造成条件寄存器损坏。
 *
 * 修复：original_opcode 保留 lowering 时写入的原始 opcode；
 *       opcode_off_back 区分指令格式（JMP=1, JNZ=2）。
 */
struct PendingJumpPatch {
    size_t offset_u16_code_index = 0; // 指向 16-bit offset 的首字节位置
    BlockId target_block_id = std::numeric_limits<BlockId>::max();
    size_t offset_base_code_index = 0; // 相对偏移基准（通常是该指令末尾）
    vm::OPCODE original_opcode = vm::OPCODE::OP_JMP; // 原始 opcode，patch 时保留
    size_t opcode_off_back = 1; // opcode 在 u16 offset 之前的字节偏移（JMP=1, JNZ=2）
};
// FUNC: 降解单函数主体。
/**
 * @brief 将单个 IR 函数降级为 VM 可执行函数对象。
 * @param module_ir 模块级 IR。
 * @param function_record 目标函数记录。
 * @return std::expected<vm::ObjFunction*, std::string> 成功返回 ObjFunction，失败返回错误信息。
 * @note 失败原因包含 span 越界、寄存器宽度不合法、立即数不支持、跳转回填失败。
 */
std::expected<vm::ObjFunction *, std::string> lowerOneFunction(const ModuleIR &module_ir,
                                                               const FuncRecord &function_record) {
    if (function_record.block_span.begin + function_record.block_span.count > module_ir.blocks.size()) {
        return std::unexpected("Function block span out of range.");
    }

    auto *function_object = new vm::ObjFunction();
    function_object->object_header.type = vm::ObjType::Function;
    function_object->object_header.isMarked = false;
    function_object->name_id = function_record.func_name_sid;
    if (function_record.arity > std::numeric_limits<uint8_t>::max()) {
        delete function_object;
        return std::unexpected("Function parameter count exceeds VM arity limit.");
    }
    function_object->arity = static_cast<uint8_t>(function_record.arity);
    function_object->max_registers = static_cast<uint16_t>(function_record.next_vreg);

    BytecodeWriter writer;
    writer.chunk.string_pool = module_ir.string_pool;
    writer.chunk.max_register_slots = static_cast<uint16_t>(function_record.next_vreg);

    std::unordered_map<BlockId, size_t> block_entry_code_index;
    std::vector<PendingJumpPatch> pending_patches;

    // BLOCK_PASS: 顺序展开 block，先记录入口 code offset。
    // 说明：编码阶段按“块顺序线性化”执行，逻辑块 id 仅用于后续跳转回填，不直接写入字节码。
    for (uint32_t relative_block_index = 0; relative_block_index < function_record.block_span.count;
         ++relative_block_index) {
        const uint32_t absolute_block_index = function_record.block_span.begin + relative_block_index;
        const BlockRecord &block_record = module_ir.blocks[absolute_block_index];

        block_entry_code_index[block_record.block_id] = writer.chunk.code.size();

        const uint32_t instruction_begin = block_record.inst_span.begin;
        const uint32_t instruction_end = instruction_begin + block_record.inst_span.count;

        for (uint32_t instruction_index = instruction_begin; instruction_index < instruction_end; ++instruction_index) {
            // INST_SNAPSHOT: 先拉平成局部变量，降低 switch 分支中的索引噪音。
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

            // ENCODE_PATTERN（只说明一次）：
            // 1) 校验操作数 kind/位宽；
            // 2) 选择 opcode；
            // 3) 按 VM 约定写入操作数字节；
            // 4) 需要延迟决议的跳转记录 patch，稍后统一回填。
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
                if (auto emitted = emitLoadConst(writer, dst_reg, constant_index, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
                break;
            }
            case InstKind::Move: {
                uint8_t dst_reg = 0;
                uint8_t src_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &dst_reg) || !requireVReg(first_kind, first_u32, &src_reg)) {
                    return std::unexpected("Move expects dst/src VReg <= 255.");
                }
                if (auto emitted = emitMove(writer, dst_reg, src_reg, source_line, source_col); !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
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
                // 同类二元指令共用一套编码路径：mapBinaryOpcode + (dst,left,right) 三寄存器写入。
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
                if (auto emitted = writer.writeRunnableOp(opcode.value(), source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
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
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_NEG, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
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
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_NOT, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
                writer.writeByte(dst_reg, source_line, source_col);
                writer.writeByte(src_reg, source_line, source_col);
                break;
            }
            case InstKind::NewArray: {
                uint8_t dst_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &dst_reg)) {
                    return std::unexpected("NewArray expects dst VReg <= 255.");
                }
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_NEW_ARRAY, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
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
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_PUSH_ARRAY, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
                writer.writeByte(array_reg, source_line, source_col);
                writer.writeByte(value_reg, source_line, source_col);
                break;
            }
            case InstKind::NewMap: {
                uint8_t dst_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &dst_reg)) {
                    return std::unexpected("NewMap expects dst VReg <= 255.");
                }
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_NEW_MAP, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
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
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_SET_MAP, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
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
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_GET_ARRAY, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
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
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_SET_ARRAY, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
                writer.writeByte(array_reg, source_line, source_col);
                writer.writeByte(index_reg, source_line, source_col);
                writer.writeByte(value_reg, source_line, source_col);
                break;
            }
            case InstKind::GetMember: {
                uint8_t dst_reg = 0;
                uint8_t object_reg = 0;
                uint8_t member_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &dst_reg) || !requireVReg(first_kind, first_u32, &object_reg) ||
                    !requireVReg(second_kind, second_u32, &member_reg)) {
                    return std::unexpected("GetMember expects dst/object/member VReg <= 255.");
                }
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_GET_PROPERTY, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
                writer.writeByte(dst_reg, source_line, source_col);
                writer.writeByte(object_reg, source_line, source_col);
                writer.writeByte(member_reg, source_line, source_col);
                break;
            }
            case InstKind::SetMember: {
                uint8_t object_reg = 0;
                uint8_t member_reg = 0;
                uint8_t value_reg = 0;
                if (!requireVReg(first_kind, first_u32, &object_reg) ||
                    !requireVReg(second_kind, second_u32, &member_reg) ||
                    !requireVReg(third_kind, third_u32, &value_reg)) {
                    return std::unexpected("SetMember expects object/member/value VReg <= 255.");
                }
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_SET_PROPERTY, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
                writer.writeByte(object_reg, source_line, source_col);
                writer.writeByte(member_reg, source_line, source_col);
                writer.writeByte(value_reg, source_line, source_col);
                break;
            }
            case InstKind::LoadGlobal: {
                uint8_t dst_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &dst_reg)) {
                    return std::unexpected("LoadGlobal expects dst VReg <= 255.");
                }
                uint32_t name_sid = 0;
                if (first_kind == ValueKind::SymbolId) {
                    name_sid = first_u32;
                } else if (first_kind == ValueKind::ImmI64) {
                    if (first_i64 < 0 || first_i64 > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
                        return std::unexpected("LoadGlobal ImmI64 payload out of uint32 range.");
                    }
                    name_sid = static_cast<uint32_t>(first_i64);
                } else {
                    return std::unexpected("LoadGlobal expects SymbolId/ImmI64 payload.");
                }
                const uint16_t constant_index = writer.addConstant(vm::Value::makeInt(static_cast<int64_t>(name_sid)));
                if (constant_index <= std::numeric_limits<uint8_t>::max()) {
                    if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_GET_GLOBAL, source_line, source_col);
                        !emitted.has_value()) {
                        return std::unexpected(emitted.error());
                    }
                    writer.writeByte(dst_reg, source_line, source_col);
                    writer.writeByte(static_cast<uint8_t>(constant_index), source_line, source_col);
                } else {
                    if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_GET_GLOBAL_W, source_line, source_col);
                        !emitted.has_value()) {
                        return std::unexpected(emitted.error());
                    }
                    writer.writeByte(dst_reg, source_line, source_col);
                    writer.writeU16(constant_index, source_line, source_col);
                }
                break;
            }
            case InstKind::StoreGlobal: {
                uint8_t value_reg = 0;
                if (!requireVReg(first_kind, first_u32, &value_reg)) {
                    return std::unexpected("StoreGlobal expects value VReg <= 255.");
                }
                uint32_t name_sid = 0;
                if (second_kind == ValueKind::SymbolId) {
                    name_sid = second_u32;
                } else if (second_kind == ValueKind::ImmI64) {
                    if (module_ir.insts.b_i64[instruction_index] < 0 ||
                        module_ir.insts.b_i64[instruction_index] >
                            static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
                        return std::unexpected("StoreGlobal ImmI64 payload out of uint32 range.");
                    }
                    name_sid = static_cast<uint32_t>(module_ir.insts.b_i64[instruction_index]);
                } else {
                    return std::unexpected("StoreGlobal expects SymbolId/ImmI64 name payload in operand b.");
                }
                const uint16_t constant_index = writer.addConstant(vm::Value::makeInt(static_cast<int64_t>(name_sid)));
                if (constant_index <= std::numeric_limits<uint8_t>::max()) {
                    if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_SET_GLOBAL, source_line, source_col);
                        !emitted.has_value()) {
                        return std::unexpected(emitted.error());
                    }
                    writer.writeByte(value_reg, source_line, source_col);
                    writer.writeByte(static_cast<uint8_t>(constant_index), source_line, source_col);
                } else {
                    if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_SET_GLOBAL_W, source_line, source_col);
                        !emitted.has_value()) {
                        return std::unexpected(emitted.error());
                    }
                    writer.writeByte(value_reg, source_line, source_col);
                    writer.writeU16(constant_index, source_line, source_col);
                }
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
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_CALL, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
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
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_JMP, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
                const size_t offset_u16_code_index = writer.chunk.code.size();
                writer.writeU16(0, source_line, source_col);
                // 仅记录目标块 id 与偏移写入点，不在这里计算真实偏移。
                pending_patches.push_back(PendingJumpPatch{
                    .offset_u16_code_index = offset_u16_code_index,
                    .target_block_id = static_cast<BlockId>(first_u32),
                    .offset_base_code_index = writer.chunk.code.size(),
                    .original_opcode = vm::OPCODE::OP_JMP,
                    .opcode_off_back = 1,
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
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_JNZ, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
                writer.writeByte(cond_reg, source_line, source_col);
                const size_t jnz_offset_u16_index = writer.chunk.code.size();
                writer.writeU16(0, source_line, source_col);
                pending_patches.push_back(PendingJumpPatch{
                    .offset_u16_code_index = jnz_offset_u16_index,
                    .target_block_id = static_cast<BlockId>(second_u32),
                    .offset_base_code_index = writer.chunk.code.size(),
                    .original_opcode = vm::OPCODE::OP_JNZ,
                    .opcode_off_back = 2,  // JNZ: opcode + reg, offset 在 reg 之后
                });
                // JMP false_target
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_JMP, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
                const size_t jmp_offset_u16_index = writer.chunk.code.size();
                writer.writeU16(0, source_line, source_col);
                pending_patches.push_back(PendingJumpPatch{
                    .offset_u16_code_index = jmp_offset_u16_index,
                    .target_block_id = static_cast<BlockId>(third_u32),
                    .offset_base_code_index = writer.chunk.code.size(),
                    .original_opcode = vm::OPCODE::OP_JMP,
                    .opcode_off_back = 1,
                });
                break;
            }
            case InstKind::Free: {
                uint8_t target_reg = 0;
                if (!requireVReg(dst_kind, dst_u32, &target_reg)) {
                    return std::unexpected("Free expects dst VReg <= 255.");
                }
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_FREE, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
                writer.writeByte(target_reg, source_line, source_col);
                break;
            }
            case InstKind::Return: {
                // RETURN_PROTO: VM 约定返回值放在逻辑寄存器 r0，然后执行 OP_RETURN。
                if (first_kind == ValueKind::VReg && first_u32 != 0u) {
                    uint8_t src_reg = 0;
                    if (!requireVReg(first_kind, first_u32, &src_reg)) {
                        return std::unexpected("Return register out of 8-bit range.");
                    }
                    if (auto emitted = emitMove(writer, 0u, src_reg, source_line, source_col); !emitted.has_value()) {
                        return std::unexpected(emitted.error());
                    }
                }
                if (auto emitted = writer.writeRunnableOp(vm::OPCODE::OP_RETURN, source_line, source_col);
                    !emitted.has_value()) {
                    return std::unexpected(emitted.error());
                }
                break;
            }
            default:
                return std::unexpected("Unsupported InstKind in MVP lowering.");
            }
        }
    }

    // CHG-20260506 PATCH_APPLY: 按目标 block 入口回填相对偏移。
    // 旧实现：所有 pending patch 统一设 jump_opcode = OP_JMP（回边时 OP_LOOP），
    //         且 opcode 位置固定为 offset_u16_code_index - 1。
    // 修复：保留 PendingJumpPatch::original_opcode 并在前向跳转时不变更；
    //       通过 opcode_off_back 区分 JMP(1字节) vs JNZ(2字节) 的指令格式。
    //       回边时仅把 JMP 翻转为 LOOP，JNZ 保持原样（条件分支始终前向）。
    for (const PendingJumpPatch &patch : pending_patches) {
        auto target_iter = block_entry_code_index.find(patch.target_block_id);
        if (target_iter == block_entry_code_index.end()) {
            return std::unexpected("Jump target block id not found during patch.");
        }
        const size_t target_code_index = target_iter->second;
        size_t relative_offset = 0;
        vm::OPCODE jump_opcode = patch.original_opcode;
        if (target_code_index < patch.offset_base_code_index) {
            // 回边：JMP → LOOP，JNZ → JNZ 不变（JNZ 始终保持前向语义）
            relative_offset = patch.offset_base_code_index - target_code_index;
            if (jump_opcode == vm::OPCODE::OP_JMP) {
                jump_opcode = vm::OPCODE::OP_LOOP;
            }
        } else {
            // 前跳：保持原 opcode
            relative_offset = target_code_index - patch.offset_base_code_index;
        }
        if (relative_offset > std::numeric_limits<uint16_t>::max()) {
            return std::unexpected("Jump offset exceeds uint16 range.");
        }
        // PATCH_OPCODE: 根据指令格式计算 opcode 字节位置。
        // JMP: opcode + u16_offset  → opcode 在 offset 前 1 字节
        // JNZ: opcode + reg + u16   → opcode 在 offset 前 2 字节
        const size_t opcode_code_index = patch.offset_u16_code_index - patch.opcode_off_back;
        writer.chunk.code[opcode_code_index] = vm::ToInt(jump_opcode);
        writer.patchU16(patch.offset_u16_code_index, static_cast<uint16_t>(relative_offset));
    }

    function_object->chunk = std::move(writer.chunk);
    function_object->chunk.module_id = module_ir.module_id;
    return function_object;
}
} // namespace
/**
 * @brief 返回 InstKind 到 VM opcode 的契约清单。
 * @return const std::vector<InstOpcodeChecklistEntry>& 映射清单引用。
 */
const std::vector<InstOpcodeChecklistEntry> &instOpcodeChecklist() { return kInstOpcodeChecklist; }

/**
 * @brief 按 function_id 降级模块内单个函数。
 * @param module_ir 模块级 IR。
 * @param function_id 目标函数 id。
 * @return std::expected<vm::ObjFunction*, std::string> 成功返回 ObjFunction，失败返回错误信息。
 */
std::expected<vm::ObjFunction *, std::string> lowerFunctionToChunk(const ModuleIR &module_ir, FuncId function_id) {
    if (function_id >= module_ir.funcs.size()) {
        return std::unexpected("Function id out of range.");
    }

    return lowerOneFunction(module_ir, module_ir.funcs[function_id]);
}
/**
 * @brief 将模块内所有函数按顺序降级为 VM 函数列表。
 * @param module_ir 模块级 IR。
 * @return std::expected<LowerResult, std::string> 成功返回函数列表，失败返回首个失败原因。
 */
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
