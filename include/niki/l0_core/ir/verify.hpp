#pragma once

#include "niki/l0_core/ir/module_ir.hpp"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/** @verify: IR 结构不变量校验层
 * Verify 的定位可以理解成“结构防火墙”：它不做类型推导，不做优化，只做一件事——
 * 在进入后端编码前，确认 IR 结构满足可执行与可编码的硬约束。
 *
 * 为什么需要这层？因为 builder 能产出“语义上看似合理”的 IR，不代表它在结构上绝对安全。
 * 例如：
 * - 基本块为空；
 * - 终结符不在块尾；
 * - block/span 越界；
 * - vreg 或 symbol 引用超范围；
 * - SoA 列长度失配。
 * 这些问题如果直接流入 lower，结果往往是崩溃、乱码字节码，或者难以定位的运行时错误。
 *
 * 从图约束看，函数本质是 CFG，CFG 至少要满足：
 * - 每个块有明确指令区间；
 * - 每个块能在末尾给出控制流去向（jump/branch/return）；
 * - 所有引用边都指向合法节点。
 * Verify 正是在做这些图不变量检查。
 *
 * 从复杂度和工程收益看，这是一遍线性扫描（大致 O(|func| + |block| + |inst|)）即可覆盖的大收益步骤，
 * 成本低、反馈早、定位准。错误被包装成 `VerifyIssue` + `VerifyErrorCode`，对测试和 CI 非常友好：
 * 测试不必匹配脆弱文本，可以稳定断言错误码。
 *
 * 这层与 ModuleIR 的 SoA 设计配套：先检查列对齐，再检查函数、块、指令和操作数引用，
 * 能保证后续任何按列读取不会在未定义状态下运行。
 */
namespace niki::ir {

//---校验错误码（按阶段语义分组）---
enum class VerifyErrorCode : uint16_t {
    None = 0,
    // FUNCTION: 函数级结构错误。
    FunctionIdMismatch,
    FunctionBlockSpanOutOfRange,
    EntryBlockOutOfRange,
    // BLOCK: 基本块级结构错误。
    BlockInstSpanOutOfRange,
    BlockEmpty,
    TerminatorNotLast,
    MissingTerminator,
    // OPERAND: 操作数引用越界错误。
    VRegOutOfRange,
    FuncRefOutOfRange,
    BlockRefOutOfRange,
    SymbolRefOutOfRange,
    StringRefOutOfRange,
    // KITS: kits 元数据结构错误。
    KitsItemSpanOutOfRange,
    KitsNameRefOutOfRange,
    KitsOwnerModuleRefOutOfRange,
    KitsAliasRefOutOfRange,
    KitsComponentRefOutOfRange,
    KitsDuplicateAliasInWindow,
    // COMPONENT: component 元数据结构错误。
    ComponentNameRefOutOfRange,
    ComponentOwnerModuleRefOutOfRange,
    ComponentSourceStructRefOutOfRange,
    // SYMBOL: 符号表结构错误。
    SymbolNameRefOutOfRange,
    SymbolOwnerModuleRefOutOfRange,
    SymbolIdMismatch,
    DuplicateExportedSymbolName,
    // TABLE: 表结构错误。
    InstColumnsMisaligned
};

//---单条校验问题---
struct VerifyIssue {
    // 结构化错误码（用于测试稳定断言与机器处理）。
    VerifyErrorCode error_code = VerifyErrorCode::None;
    // 人类可读错误描述。
    std::string message;

    // LOCATION: 问题定位信息（无法定位时保持 uint32_max）。
    // 函数下标（module_ir.funcs）。
    uint32_t func_idx = UINT32_MAX;
    // 函数内相对块下标。
    uint32_t rel_block_idx = UINT32_MAX;
    // 块内相对指令下标。
    uint32_t inst_idx = UINT32_MAX;
};

//---校验报告（可聚合多问题）---
struct VerifyReport {
    // 全部校验问题。
    std::vector<VerifyIssue> issues;
    // 快速通过判断（无问题即通过）。
    bool ok() const { return issues.empty(); }
    // 追加一条问题记录。
    void add(VerifyErrorCode code, std::string msg, uint32_t func_idx = UINT32_MAX, uint32_t rel_block_idx = UINT32_MAX,
             uint32_t inst_idx = UINT32_MAX) {
        issues.push_back(VerifyIssue{code, std::move(msg), func_idx, rel_block_idx, inst_idx});
    }
};
//---扁平 IR 校验入口---
// 输入：ModuleIR
// 输出：VerifyReport（issues 为空表示通过）
VerifyReport verifyModuleIRFlat(const ModuleIR &module);
} // namespace niki::ir