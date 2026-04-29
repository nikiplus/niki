#pragma once
#include "niki/l0_core/vm/opcode.hpp"
#include <array>
#include <cstdint>
#include <string_view>

namespace niki::vm {
/*
 * opcode_capobility.hpp —— 指令能力表（Lowering/VM 协议闸门）。
 *
 * 该表把“语法上存在的 opcode”与“当前运行时是否可执行”显式分离：
 * - Implemented: VM 已实现，可执行；
 * - LoweringBlocked: VM 或语义尚未完整，lowering 阶段禁止发射；
 * - Planned: 保留位/未来规划。
 *
 * 这样可以在编译阶段尽早阻断不可运行路径，避免把错误留到运行时。
 */
enum class OpcodeCapability : uint8_t {
    Implemented,     // VM 已实现
    LoweringBlocked, // lowering 禁止发射
    Planned          // 计划实现，当前不可用
};
struct OpcodeInfo {
    OPCODE opcode; ///< 指令枚举值。
    std::string_view name; ///< 指令名文本（用于日志/诊断）。
    OpcodeCapability capability; ///< 当前能力状态。
};
constexpr auto kOpcodeInfos = std::array{
    // Calc
    OpcodeInfo{OPCODE::OP_IADD, "OP_IADD", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_ISUB, "OP_ISUB", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_IMUL, "OP_IMUL", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_IDIV, "OP_IDIV", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_IMOD, "OP_IMOD", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_CONCAT, "OP_CONCAT", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_DICE, "OP_DICE", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_FADD, "OP_FADD", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_FSUB, "OP_FSUB", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_FMUL, "OP_FMUL", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_FDIV, "OP_FDIV", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_IEQ, "OP_IEQ", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_INE, "OP_INE", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_ILT, "OP_ILT", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_IGT, "OP_IGT", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_ILE, "OP_ILE", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_IGE, "OP_IGE", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_FEQ, "OP_FEQ", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_FNE, "OP_FNE", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_FLT, "OP_FLT", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_FGT, "OP_FGT", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_FLE, "OP_FLE", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_FGE, "OP_FGE", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_SEQ, "OP_SEQ", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_SNE, "OP_SNE", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_OEQ, "OP_OEQ", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_ONE, "OP_ONE", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_AND, "OP_AND", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_OR, "OP_OR", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_BIT_AND, "OP_BIT_AND", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_BIT_OR, "OP_BIT_OR", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_BIT_XOR, "OP_BIT_XOR", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_BIT_SHL, "OP_BIT_SHL", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_BIT_SHR, "OP_BIT_SHR", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_NOT, "OP_NOT", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_BIT_NOT, "OP_BIT_NOT", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_NEG, "OP_NEG", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_FNEG, "OP_FNEG", OpcodeCapability::Implemented},

    // Control
    OpcodeInfo{OPCODE::OP_JMP, "OP_JMP", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_LOOP, "OP_LOOP", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_JNZ, "OP_JNZ", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_JZ, "OP_JZ", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_CALL, "OP_CALL", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_INVOKE, "OP_INVOKE", OpcodeCapability::LoweringBlocked},
    OpcodeInfo{OPCODE::OP_RETURN, "OP_RETURN", OpcodeCapability::Implemented},

    // Data
    OpcodeInfo{OPCODE::OP_DEFINE_GLOBAL, "OP_DEFINE_GLOBAL", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_DEFINE_GLOBAL_W, "OP_DEFINE_GLOBAL_W", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_GET_GLOBAL, "OP_GET_GLOBAL", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_GET_GLOBAL_W, "OP_GET_GLOBAL_W", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_SET_GLOBAL, "OP_SET_GLOBAL", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_SET_GLOBAL_W, "OP_SET_GLOBAL_W", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_NEW_INSTANCE, "OP_NEW_INSTANCE", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_GET_FIELD, "OP_GET_FIELD", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_SET_FIELD, "OP_SET_FIELD", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_NEW_MAP, "OP_NEW_MAP", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_SET_MAP, "OP_SET_MAP", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_GET_MAP, "OP_GET_MAP", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_NEW_ARRAY, "OP_NEW_ARRAY", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_PUSH_ARRAY, "OP_PUSH_ARRAY", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_GET_ARRAY, "OP_GET_ARRAY", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_SET_ARRAY, "OP_SET_ARRAY", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_GET_PROPERTY, "OP_GET_PROPERTY", OpcodeCapability::LoweringBlocked},
    OpcodeInfo{OPCODE::OP_SET_PROPERTY, "OP_SET_PROPERTY", OpcodeCapability::LoweringBlocked},
    OpcodeInfo{OPCODE::OP_METHOD, "OP_METHOD", OpcodeCapability::LoweringBlocked},
    OpcodeInfo{OPCODE::OP_TRUE, "OP_TRUE", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_FALSE, "OP_FALSE", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_NIL, "OP_NIL", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_LOAD_CONST, "OP_LOAD_CONST", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_LOAD_CONST_W, "OP_LOAD_CONST_W", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_MOVE, "OP_MOVE", OpcodeCapability::Implemented},
    OpcodeInfo{OPCODE::OP_FREE, "OP_FREE", OpcodeCapability::Implemented},
};
/** @brief 查询 opcode 的能力标记。 */
constexpr OpcodeCapability capabilityOf(OPCODE op) {
    for (const auto &info : kOpcodeInfos) {
        if (info.opcode == op)
            return info.capability;
    }
    return OpcodeCapability::Planned;
}
/** @brief 查询 opcode 的文本名。 */
constexpr std::string_view nameOf(OPCODE op) {
    for (const auto &info : kOpcodeInfos) {
        if (info.opcode == op)
            return info.name;
    }
    return "OP_UNKNOWN";
}
/** @brief 判断 opcode 当前是否可执行。 */
constexpr bool isRunnable(OPCODE op) { return capabilityOf(op) == OpcodeCapability::Implemented; }
} // namespace niki::vm