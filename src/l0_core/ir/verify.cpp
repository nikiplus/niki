#include "niki/l0_core/ir/verify.hpp"
#include <array>

/** @verify_impl: 扁平 IR 结构校验实现
 * 这个文件实现 verify 核心算法：在 lowering 前线性扫描 ModuleIR，收集结构违规项。
 * 它不做优化、不改写 IR，只做约束检查并给出可定位的问题集合。
 *
 * 校验顺序遵循“由粗到细”的防御策略：
 * 先检查 SoA 列对齐，再检查函数/块区间，再检查终结符位置，最后检查操作数引用域。
 * 这样可以在最早阶段截获可能导致越界的结构问题，避免后续校验建立在不可靠前提上。
 *
 * 输出采用 VerifyReport 聚合而非首错即停，目的是一次性暴露更多结构问题，提升调试效率。
 *
 * 字段流说明（核心）：
 * - `module_ir.funcs` -> 函数级循环：驱动每个函数的 span/entry 校验。
 * - `FuncRecord.block_span` -> `module_ir.blocks` 切片：定位该函数的块集合。
 * - `BlockRecord.inst_span` -> `module_ir.insts.*` 列：定位该块的指令窗口。
 * - `dst/a/b/c kind + payload` -> 引用范围检查：验证 vreg/func/block/symbol/string 的合法性。
 * - 校验发现问题 -> `VerifyReport::issues`：统一聚合输出。
 */
namespace niki::ir {

//------------------------------------------------------------------------------
// HELPER: 局部辅助函数，封装终结符与引用判定。
//------------------------------------------------------------------------------
static bool isTerminator(InstKind k) {
    return k == InstKind::Jump || k == InstKind::Branch || k == InstKind::Return;
}
//------------------------------------------------------------------------------
// VERIFY: Flat IR 主校验流程。
//------------------------------------------------------------------------------
/**
 * @brief 校验扁平 IR 的结构一致性与引用合法性。
 * @param module_ir 待校验模块 IR。
 * @return VerifyReport 校验问题集合；issues 为空表示通过。
 * @note 除 InstTable 列错位外，其它问题均以 issue 累计上报。
 */
VerifyReport verifyModuleIRFlat(const ModuleIR &module_ir) {
    VerifyReport report;

    // PASS_0: SoA 列对齐是最基础结构不变量。
    if (!module_ir.insts.aligned()) {
        report.add(VerifyErrorCode::InstColumnsMisaligned, "inst table columns are not aligned");
        return report;
    }

    // PASS_PLAN（只说明一次）：
    // PASS_1 函数级不变量 -> PASS_2 块级区间/终结符 -> PASS_3 指令引用域。
    // 先大后小的顺序可避免在无效区间上继续深层校验。
    // PASS_1: 函数级结构校验。
    for (uint32_t function_index = 0; function_index < module_ir.funcs.size(); ++function_index) {
        const FuncRecord &function_record = module_ir.funcs[function_index];

        if (function_record.func_id != function_index)
            report.add(VerifyErrorCode::FunctionIdMismatch, "func_id mismatch", function_index);

        if (function_record.block_span.begin + function_record.block_span.count > module_ir.blocks.size()) {
            report.add(VerifyErrorCode::FunctionBlockSpanOutOfRange, "function block span out of range", function_index);
            continue;
        }

        if (function_record.entry_block >= function_record.block_span.count) {
            report.add(VerifyErrorCode::EntryBlockOutOfRange, "entry block out of function block span",
                       function_index);
        }

        // PASS_2: 基本块级 span 与终结符校验。
        for (uint32_t relative_block_index = 0; relative_block_index < function_record.block_span.count;
             ++relative_block_index) {
            const uint32_t absolute_block_index = function_record.block_span.begin + relative_block_index;
            const BlockRecord &block_record = module_ir.blocks[absolute_block_index];

            if (block_record.inst_span.begin + block_record.inst_span.count > module_ir.insts.size()) {
                report.add(VerifyErrorCode::BlockInstSpanOutOfRange, "block inst span out of range", function_index,
                           relative_block_index);
                continue;
            }

            if (block_record.inst_span.count == 0) {
                report.add(VerifyErrorCode::BlockEmpty, "block has no instruction", function_index, relative_block_index);
                continue;
            }

            const uint32_t block_inst_begin = block_record.inst_span.begin;
            const uint32_t block_inst_end = block_inst_begin + block_record.inst_span.count;

            // PASS_3: 指令级与引用合法性校验。
            for (uint32_t instruction_absolute_index = block_inst_begin; instruction_absolute_index < block_inst_end;
                 ++instruction_absolute_index) {
                const bool current_is_terminator = isTerminator(module_ir.insts.kind[instruction_absolute_index]);
                if (current_is_terminator && instruction_absolute_index + 1 != block_inst_end) {
                    report.add(VerifyErrorCode::TerminatorNotLast, "terminator must be last in block", function_index,
                               relative_block_index, instruction_absolute_index - block_inst_begin);
                }

                const std::array<ValueKind, 4> operand_kinds = {module_ir.insts.dst_kind[instruction_absolute_index],
                                                                module_ir.insts.a_kind[instruction_absolute_index],
                                                                module_ir.insts.b_kind[instruction_absolute_index],
                                                                module_ir.insts.c_kind[instruction_absolute_index]};
                const std::array<uint32_t, 4> operand_u32_payloads = {
                    module_ir.insts.dst_u32[instruction_absolute_index],
                    module_ir.insts.a_u32[instruction_absolute_index],
                    module_ir.insts.b_u32[instruction_absolute_index],
                    module_ir.insts.c_u32[instruction_absolute_index]};

                // OPERAND_PASS: 同一规则校验 dst/a/b/c 四个槽位引用合法性（模板化处理，避免重复逻辑）。
                for (int operand_slot_index = 0; operand_slot_index < 4; ++operand_slot_index) {
                    const ValueKind operand_kind = operand_kinds[operand_slot_index];
                    const uint32_t operand_u32_payload = operand_u32_payloads[operand_slot_index];

                    if (operand_kind == ValueKind::VReg && operand_u32_payload >= function_record.next_vreg) {
                        report.add(VerifyErrorCode::VRegOutOfRange, "vreg out of range", function_index, relative_block_index,
                                   instruction_absolute_index - block_inst_begin);
                    } else if (operand_kind == ValueKind::FuncId && operand_u32_payload >= module_ir.funcs.size()) {
                        report.add(VerifyErrorCode::FuncRefOutOfRange, "func ref out of range", function_index,
                                   relative_block_index, instruction_absolute_index - block_inst_begin);
                    } else if (operand_kind == ValueKind::BlockId &&
                               operand_u32_payload >= function_record.block_span.count) {
                        report.add(VerifyErrorCode::BlockRefOutOfRange, "block ref out of range in function", function_index,
                                   relative_block_index, instruction_absolute_index - block_inst_begin);
                    } else if (operand_kind == ValueKind::SymbolId && operand_u32_payload >= module_ir.syms.size()) {
                        report.add(VerifyErrorCode::SymbolRefOutOfRange, "symbol ref out of range", function_index,
                                   relative_block_index, instruction_absolute_index - block_inst_begin);
                    } else if (operand_kind == ValueKind::StringId &&
                               operand_u32_payload >= module_ir.string_pool.size()) {
                        report.add(VerifyErrorCode::StringRefOutOfRange, "string ref out of range", function_index,
                                   relative_block_index, instruction_absolute_index - block_inst_begin);
                    }
                }
            }

            const InstKind block_tail_inst_kind = module_ir.insts.kind[block_inst_end - 1];
            if (!isTerminator(block_tail_inst_kind)) {
                report.add(VerifyErrorCode::MissingTerminator, "block missing terminator", function_index,
                           relative_block_index, block_record.inst_span.count - 1);
            }
        }
    }

    return report;
}
} // namespace niki::ir