#include "niki/l0_core/ir/verify.hpp"
#include <cstdint>
#include <string>
#include <unordered_set>

namespace niki::ir {
namespace {

/*
设计摘要（控制流 + 底层执行模型）：

0) 先看底层：CPU 实际在做什么
   - CPU 的核心循环是：取指(Fetch) -> 译码(Decode) -> 执行(Execute) -> 更新 PC(Program Counter)。
   - 对“普通顺序指令”，PC 通常自增到下一条机器指令。
   - 对“控制流指令”（跳转/分支/返回），PC 会被显式改写为目标地址。
   - 因此程序并非“停住”，而是在不同上下文/地址间持续转移执行。

1) 文本语言在编译过程中会被结构化
   - 源码最初是线性文本；进入 IR 后变成控制流图（CFG）。
   - 节点是 IRBasicBlock，边由 Jump/Branch/Return 等终止符给出。
   - 这种图结构是对“PC 可能如何变化”的静态建模。

2) 基本块内承载什么
   - 块内承载“顺序执行区间”的计算与数据搬运（Add/Move/Load/Store/Call 等）。
   - 从微架构角度看，可近似理解为“PC 按顺序前进的一段指令窗”。
   - 这些指令描述“做什么”；不负责最终决定“下一个块去哪”。

3) 终止符承载什么
   - 终止符（Jump/Branch/Return）是块级控制流决策点，本质是“改写下一步 PC 的来源”。
   - Jump：无条件指定下一块（下一 PC）。
   - Branch：按条件在两个目标块间选择下一块（下一 PC）。
   - Return：把控制权交还调用者（恢复调用约定相关状态后继续执行）。
   - break 在 IR 中通常降解为跳到循环出口块的 Jump/Branch，而非“停止执行”。

4) 为什么 verify 要强制 terminator 规则
   - 每个基本块必须在末尾显式给出控制流去向，避免隐式 fallthrough（文本邻接误当执行邻接）。
   - 若块为空或末尾无 terminator，后端无法确定“下一 PC”，CFG 不闭合，lowering/linking 语义不稳定。
   - 这也是将高级语法（if/loop/break/return）映射为可验证执行语义的关键约束。

5) 为什么强调“终止符只能在块尾”
   - 若终止符出现在中间，后续指令在硬件语义上会变成不可达或歧义路径。
   - 把终止符限制在块尾，可保证单块内部语义是“先顺序，再一次性转移”。
*/

// Why:
// - 基本块必须以“控制流终结指令”结束，才能形成合法 CFG。
// - 若允许块尾不是终结指令，后续 lowering/linking 对后继块关系将不确定。
bool isTerminator(IRInstKind inst_kind) {
    return inst_kind == IRInstKind::Jump || inst_kind == IRInstKind::Branch || inst_kind == IRInstKind::Return;
}

// Why:
// - 当前 IR 约定 destination 只用于“写入结果寄存器”。
// - 允许 Invalid 是为了支持无目标写入的指令（如部分 Return/Jump）。
bool isDestinationKindAllowed(IRValueKind val_kind) {
    return val_kind == IRValueKind::Invalid || val_kind == IRValueKind::VReg;
}

// Why:
// - VReg 引用必须在函数已分配范围内，越界说明 builder 或手工构造有错误。
bool isValidVirtualRegister(const IRFunction &func_ir, const IRValue &value) {
    if (value.value_kind != IRValueKind::VReg) {
        return true;
    }
    return value.payload_as_u32 < func_ir.next_vreg_id;
}

// Why:
// - FuncId 是 module 级引用，必须落在 func_table 范围内。
bool isValidFunctionReference(const ModuleIR &mod_ir, const IRValue &value) {
    if (value.value_kind != IRValueKind::FuncId) {
        return true;
    }
    return value.payload_as_u32 < mod_ir.func_table.size();
}

// Why:
// - BlockId 是 function 级引用，必须落在当前函数 basic_blocks 范围内。
bool isValidBlockReference(const IRFunction &func_ir, const IRValue &value) {
    if (value.value_kind != IRValueKind::BlockId) {
        return true;
    }
    return value.payload_as_u32 < func_ir.basic_blocks.size();
}

// Why:
// - SymbolId 由 module 符号表统一分配，越界代表符号绑定错误。
bool isValidSymbolReference(const ModuleIR &mod_ir, const IRValue &value) {
    if (value.value_kind != IRValueKind::SymbolId) {
        return true;
    }
    return value.payload_as_u32 < mod_ir.sym_table.size();
}

// Why:
// - StringId 指向模块字符串池，越界会导致名称解析/报错渲染异常。
bool isValidStringReference(const ModuleIR &mod_ir, const IRValue &value) {
    if (value.value_kind != IRValueKind::StringId) {
        return true;
    }
    return value.payload_as_u32 < mod_ir.module_string_pool.size();
}
 
void verifyOneOperandReference(VerifyReport &report, const ModuleIR &mod_ir, const IRFunction &func_ir,
                               const IRValue &operand, uint32_t function_id, uint32_t block_id,
                               uint32_t inst_idx) {
    // How:
    // - 一个操作数可能是多种 kind，本函数只在对应 kind 时触发范围检查；
    // - 非对应 kind 直接放行，避免重复写 switch 并降低维护成本。
    if (!isValidVirtualRegister(func_ir, operand)) {
        report.addIssue(VerifyErrorCode::InvalidVirtualRegisterIdentifier, "Virtual register out of range.",
                        function_id, block_id, inst_idx);
    }
    if (!isValidFunctionReference(mod_ir, operand)) {
        report.addIssue(VerifyErrorCode::InvalidFunctionReference, "Function reference out of range.", function_id,
                        block_id, inst_idx);
    }
    if (!isValidBlockReference(func_ir, operand)) {
        report.addIssue(VerifyErrorCode::InvalidBlockReference, "Block reference out of range.", function_id, block_id,
                        inst_idx);
    }
    if (!isValidSymbolReference(mod_ir, operand)) {
        report.addIssue(VerifyErrorCode::InvalidSymbolReference, "Symbol reference out of range.", function_id,
                        block_id, inst_idx);
    }
    if (!isValidStringReference(mod_ir, operand)) {
        report.addIssue(VerifyErrorCode::InvalidStringReference, "String reference out of range.", function_id,
                        block_id, inst_idx);
    }
}

void verifyInstructionShape(VerifyReport &report, const IRInst &inst, uint32_t function_id, uint32_t block_id,
                            uint32_t inst_idx) {
    const auto addInvalidSourceKindIssue = [&](const std::string &message) {
        report.addIssue(VerifyErrorCode::InvalidSourceValueKind, message, function_id, block_id, inst_idx);
    };
    const auto expectSourceKind = [&](const IRValue &value, IRValueKind expected_kind, const std::string &message) {
        if (value.value_kind != expected_kind) {
            addInvalidSourceKindIssue(message);
        }
    };
    const auto expectSourceKindOneOf = [&](const IRValue &value, IRValueKind kind_a, IRValueKind kind_b,
                                           const std::string &message) {
        if (value.value_kind != kind_a && value.value_kind != kind_b) {
            addInvalidSourceKindIssue(message);
        }
    };

    // 通用约束: dst 仅允许 Invalid 或 VReg
    if (!isDestinationKindAllowed(inst.destination_value.value_kind)) {
        report.addIssue(VerifyErrorCode::InvalidDestinationValueKind, "Destination value must be Invalid or VReg.",
                        function_id, block_id, inst_idx);
    }
    // MVP 最小形状校验
    // Why:
    // - 这里校验的是“结构完整性”而非“类型正确性”；
    // - 类型匹配（int/float/bool）应由 semantic 或后续专门 pass 处理。
    switch (inst.instruction_kind) {
    case IRInstKind::Nop:
        break;

    case IRInstKind::Constant:
        // 期望有 destination，source 在 first_operand（立即数/字符串等）
        if (inst.destination_value.value_kind != IRValueKind::VReg) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout, "Constant expects VReg destination.",
                            function_id, block_id, inst_idx);
        }
        break;

    case IRInstKind::Move:
        if (inst.destination_value.value_kind != IRValueKind::VReg || inst.first_operand.value_kind == IRValueKind::Invalid) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout,
                            "Move expects VReg destination and valid source operand.", function_id, block_id, inst_idx);
        }
        break;

    case IRInstKind::Add:
    case IRInstKind::Sub:
    case IRInstKind::Mul:
    case IRInstKind::Div:
    case IRInstKind::Mod:
    case IRInstKind::CmpEq:
    case IRInstKind::CmpNe:
    case IRInstKind::CmpLt:
    case IRInstKind::CmpLe:
    case IRInstKind::CmpGt:
    case IRInstKind::CmpGe:
    case IRInstKind::LogicAnd:
    case IRInstKind::LogicOr:
        if (inst.destination_value.value_kind != IRValueKind::VReg || inst.first_operand.value_kind == IRValueKind::Invalid ||
            inst.second_operand.value_kind == IRValueKind::Invalid) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout,
                            "Binary-like instruction expects dest + 2 operands.", function_id, block_id, inst_idx);
        }
        break;

    case IRInstKind::Neg:
    case IRInstKind::LogicNot:
        if (inst.destination_value.value_kind != IRValueKind::VReg || inst.first_operand.value_kind == IRValueKind::Invalid) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout,
                            "Unary-like instruction expects dst + 1 operand.", function_id, block_id, inst_idx);
        }
        break;
    case IRInstKind::LoadGlobal:
        if (inst.destination_value.value_kind != IRValueKind::VReg || inst.first_operand.value_kind == IRValueKind::Invalid) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout,
                            "LoadGlobal expects dst + symbol operand.", function_id, block_id, inst_idx);
        } else {
            expectSourceKind(inst.first_operand, IRValueKind::SymbolId, "LoadGlobal first operand must be SymbolId.");
        }
        break;

    case IRInstKind::StoreGlobal:
        if (inst.first_operand.value_kind == IRValueKind::Invalid || inst.second_operand.value_kind == IRValueKind::Invalid) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout,
                            "StoreGlobal expects symbol/value operands.", function_id, block_id, inst_idx);
        } else {
            expectSourceKind(inst.first_operand, IRValueKind::SymbolId, "StoreGlobal first operand must be SymbolId.");
        }
        break;

    case IRInstKind::Call:
        // 约定：destination 可为 Invalid 或 VReg
        // first_operand 通常是 FuncId / SymbolId / VReg(callee)
        if (inst.first_operand.value_kind == IRValueKind::Invalid) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout, "Call expects valid callee operand.",
                            function_id, block_id, inst_idx);
        } else {
            expectSourceKindOneOf(inst.first_operand, IRValueKind::VReg, IRValueKind::FuncId,
                                  "Call callee operand must be VReg or FuncId.");
            if (inst.auxiliary_data > 0) {
                expectSourceKind(inst.second_operand, IRValueKind::VReg,
                                 "Call second operand must be argument base VReg when arg_count > 0.");
            }
        }
        break;
    case IRInstKind::NewArray:
    case IRInstKind::NewMap:
        if (inst.destination_value.value_kind != IRValueKind::VReg) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout,
                            "NewArray/NewMap expects VReg destination.", function_id, block_id, inst_idx);
        }
        break;
    case IRInstKind::PushArray:
        if (inst.first_operand.value_kind != IRValueKind::VReg || inst.second_operand.value_kind == IRValueKind::Invalid) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout,
                            "PushArray expects array VReg and element operand.", function_id, block_id, inst_idx);
        }
        break;
    case IRInstKind::SetMap:
        if (inst.first_operand.value_kind == IRValueKind::Invalid || inst.second_operand.value_kind == IRValueKind::Invalid ||
            inst.third_operand.value_kind == IRValueKind::Invalid) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout,
                            "Set* instruction expects target/key(or member)/value operands.", function_id, block_id,
                            inst_idx);
        }
        break;
    case IRInstKind::SetIndex:
        if (inst.first_operand.value_kind != IRValueKind::VReg || inst.second_operand.value_kind != IRValueKind::VReg ||
            inst.third_operand.value_kind == IRValueKind::Invalid) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout,
                            "SetIndex expects target VReg, index VReg and value operand.", function_id, block_id,
                            inst_idx);
        }
        break;
    case IRInstKind::SetMember:
        if (inst.first_operand.value_kind != IRValueKind::VReg || inst.second_operand.value_kind == IRValueKind::Invalid ||
            inst.third_operand.value_kind == IRValueKind::Invalid) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout,
                            "SetMember expects target VReg, member StringId and value operand.", function_id, block_id,
                            inst_idx);
        } else {
            expectSourceKind(inst.second_operand, IRValueKind::StringId,
                             "SetMember second operand must be StringId.");
        }
        break;
    case IRInstKind::GetIndex:
        if (inst.destination_value.value_kind != IRValueKind::VReg || inst.first_operand.value_kind == IRValueKind::Invalid ||
            inst.second_operand.value_kind == IRValueKind::Invalid) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout,
                            "Get* instruction expects dst + target + key(or member).", function_id, block_id, inst_idx);
        }
        break;
    case IRInstKind::GetMember:
        if (inst.destination_value.value_kind != IRValueKind::VReg || inst.first_operand.value_kind != IRValueKind::VReg ||
            inst.second_operand.value_kind == IRValueKind::Invalid) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout,
                            "GetMember expects dst + target VReg + member StringId.", function_id, block_id, inst_idx);
        } else {
            expectSourceKind(inst.second_operand, IRValueKind::StringId,
                             "GetMember second operand must be StringId.");
        }
        break;
    case IRInstKind::Return:
        // 允许无返回值
        break;

    case IRInstKind::Jump:
        if (inst.first_operand.value_kind != IRValueKind::BlockId) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout, "Jump expects BlockId operand.",
                            function_id, block_id, inst_idx);
        }
        break;

    case IRInstKind::Branch:
        if (inst.first_operand.value_kind == IRValueKind::Invalid || inst.second_operand.value_kind != IRValueKind::BlockId ||
            inst.third_operand.value_kind != IRValueKind::BlockId) {
            report.addIssue(VerifyErrorCode::InvalidInstructionOperandLayout,
                            "Branch expects cond + true_block + false_block.", function_id, block_id, inst_idx);
        }
        break;

    case IRInstKind::Phi:
        // MVP暂不做严格校验，仅保留形状入口
        break;
    }
}

void verifyCallArgumentCountIfDirect(VerifyReport &report, const ModuleIR &mod_ir, const IRInst &inst,
                                     uint32_t function_id, uint32_t block_id, uint32_t inst_idx) {
    // Why:
    // - 仅 direct FuncId 才能在 verify 阶段静态拿到签名并做个数校验。
    // - 对 SymbolId/VReg 间接调用，参数校验通常需要更晚阶段信息（链接或运行时）。
    if (inst.instruction_kind != IRInstKind::Call) {
        return;
    }
    // 约定：auxiliary_data = 实参个数
    uint32_t arg_count = inst.auxiliary_data;

    // 仅在 direct FuncId 调用时做静态个数校验
    if (inst.first_operand.value_kind != IRValueKind::FuncId)
        return;

    uint32_t callee_func_id = inst.first_operand.payload_as_u32;
    if (callee_func_id >= mod_ir.func_table.size()) {
        return; // 越界已由通用引用检查报告
    }

    const IRFunction &callee_func = mod_ir.func_table[callee_func_id];
    uint32_t expected_param_count = static_cast<uint32_t>(callee_func.func_sig.parameter_types.size());

    if (arg_count != expected_param_count) {
        report.addIssue(VerifyErrorCode::CallArgumentCountMismatch,
                        "Call argument count mismatch: expected " + std::to_string(expected_param_count) + ", got " +
                            std::to_string(arg_count) + ".",
                        function_id, block_id, inst_idx);
    }
}
} // namespace

VerifyReport verifyModuleIR(const ModuleIR &mod_ir) {
    VerifyReport report;

    if (mod_ir.hasInitializerFunc() && mod_ir.module_initializer_func_id >= mod_ir.func_table.size()) {
        report.addIssue(VerifyErrorCode::InvalidInitializerFunctionIdentifier,
                        "Initializer function identifier out of range.");
    }

    std::unordered_set<uint32_t> seen_symbol_name_ids;
    for (const IRSymbol &symbol : mod_ir.sym_table) {
        if (!seen_symbol_name_ids.insert(symbol.sym_name_id).second) {
            report.addIssue(VerifyErrorCode::DuplicateSymbolNameIdentifier,
                            "Duplicate symbol name identifier found.");
        }
    }

    for (const IRFunction &func_ir : mod_ir.func_table) {
        VerifyReport func_report = verifyFuncIR(mod_ir, func_ir);
        report.issues.insert(report.issues.end(), func_report.issues.begin(), func_report.issues.end());
    }
    return report;
}

VerifyReport verifyFuncIR(const ModuleIR &mod_ir, const IRFunction &func_ir) {
    VerifyReport report;

    const uint32_t func_id = func_ir.func_id;

    // 1) func id 与表位置的一致性（若能在表中找到）
    // Why:
    // - 本项目默认“ID==vector 下标”契约；若不一致，后续任何跨表引用都可能错位。
    if (func_id >= mod_ir.func_table.size() || &mod_ir.func_table[func_id] != &func_ir) {
        report.addIssue(VerifyErrorCode::InvalidFunctionIdentifier,
                        "Function identifier is not consistent with func_table index.", func_id);
    }

    // 2) entry block 合法
    // Why:
    // - entry_block_id 是函数执行入口，必须指向现有基本块。
    if (func_ir.entry_block_id >= func_ir.basic_blocks.size()) {
        report.addIssue(VerifyErrorCode::InvalidEntryBlockIdentifier, "Entry block identifier out of range.",
                        func_id);
    }

    // 3) block id 唯一性 + 合法性
    // Why:
    // - block id 重复会让 branch/jump 目标语义歧义；
    // - block id 越界会导致 CFG 不可构建。
    std::unordered_set<uint32_t> seen_block_ids;
    for (uint32_t blk_idx = 0; blk_idx < func_ir.basic_blocks.size(); ++blk_idx) {
        const IRBasicBlock &block = func_ir.basic_blocks[blk_idx];
        const uint32_t block_id = block.block_id;

        if (block_id >= func_ir.basic_blocks.size()) {
            report.addIssue(VerifyErrorCode::InvalidBlockIdentifier, "Block identifier out of range.",
                            func_id, block_id);
        }

        if (!seen_block_ids.insert(block_id).second) {
            report.addIssue(VerifyErrorCode::DuplicateBlockIdentifier, "Duplicate block identifier found.",
                            func_id, block_id);
        }

        // 4) block 至少应该有 terminator
        // Why:
        // - 空块没有控制流去向，属于结构错误；
        // - terminator 不在尾部也会破坏单入口-单出口的基本块约束。
        if (block.instruction_list.empty()) {
            report.addIssue(VerifyErrorCode::EmptyBlockWithoutTerminator, "Basic block has no instructions.",
                            func_id, block_id);
            continue;
        }

        const uint32_t last_inst_idx = static_cast<uint32_t>(block.instruction_list.size() - 1);
        for (uint32_t inst_idx = 0; inst_idx < block.instruction_list.size(); ++inst_idx) {
            const IRInst &inst = block.instruction_list[inst_idx];

            verifyInstructionShape(report, inst, func_id, block_id, inst_idx);

            verifyOneOperandReference(report, mod_ir, func_ir, inst.destination_value, func_id, block_id,
                                      inst_idx);

            verifyOneOperandReference(report, mod_ir, func_ir, inst.first_operand, func_id, block_id,
                                      inst_idx);

            verifyOneOperandReference(report, mod_ir, func_ir, inst.second_operand, func_id, block_id,
                                      inst_idx);

            verifyOneOperandReference(report, mod_ir, func_ir, inst.third_operand, func_id, block_id,
                                      inst_idx);

            verifyCallArgumentCountIfDirect(report, mod_ir, inst, func_id, block_id, inst_idx);

            const bool is_current_terminator = isTerminator(inst.instruction_kind);
            if (inst_idx != last_inst_idx && is_current_terminator) {
                report.addIssue(VerifyErrorCode::InvalidTerminatorInstruction,
                                "Terminator instruction must be the last instruction in the block.", func_id, block_id,
                                inst_idx);
            }
        }
        const IRInst &term_inst = block.instruction_list.back();
        if (!isTerminator(term_inst.instruction_kind)) {
            report.addIssue(VerifyErrorCode::MissingTerminatorInstruction,
                            "Basic block must end with Jump/Branch/Return terminator.", func_id, block_id,
                            last_inst_idx);
        }
    }
    return report;
}

bool hasStructuralErrors(const VerifyReport &report) {
    // Why:
    // - 这是“能否继续编译管线”的快速判定；
    // - 返回 true 的都是会破坏 IR 结构/引用安全的错误。
    for (const VerifyIssue &issue : report.issues) {
        switch (issue.error_code) {
        case VerifyErrorCode::InvalidInitializerFunctionIdentifier:
        case VerifyErrorCode::InvalidFunctionIdentifier:
        case VerifyErrorCode::InvalidEntryBlockIdentifier:
        case VerifyErrorCode::InvalidBlockIdentifier:
        case VerifyErrorCode::DuplicateBlockIdentifier:
        case VerifyErrorCode::EmptyBlockWithoutTerminator:
        case VerifyErrorCode::MissingTerminatorInstruction:
        case VerifyErrorCode::InvalidTerminatorInstruction:
        case VerifyErrorCode::InvalidInstructionOperandLayout:
        case VerifyErrorCode::InvalidDestinationValueKind:
        case VerifyErrorCode::InvalidSourceValueKind:
        case VerifyErrorCode::InvalidVirtualRegisterIdentifier:
        case VerifyErrorCode::InvalidFunctionReference:
        case VerifyErrorCode::InvalidBlockReference:
        case VerifyErrorCode::InvalidSymbolReference:
        case VerifyErrorCode::InvalidStringReference:
            return true;
        case VerifyErrorCode::None:
        case VerifyErrorCode::DuplicateSymbolNameIdentifier:
        case VerifyErrorCode::CallArgumentCountMismatch:
            break;
        }
    }
    return false;
}

} // namespace niki::ir