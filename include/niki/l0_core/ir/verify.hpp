#pragma once

#include "niki/l0_core/ir/module_ir.hpp"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
namespace niki::ir {

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
    // TABLE: 表结构错误。
    InstColumnsMisaligned
};

struct VerifyIssue {
    VerifyErrorCode error_code = VerifyErrorCode::None;
    std::string message;

    // LOCATION: 问题定位信息（无法定位时保持 uint32_max）。
    uint32_t func_idx = UINT32_MAX;
    uint32_t rel_block_idx = UINT32_MAX;
    uint32_t inst_idx = UINT32_MAX;
};

struct VerifyReport {
    std::vector<VerifyIssue> issues;
    bool ok() const { return issues.empty(); }
    void add(VerifyErrorCode code, std::string msg, uint32_t func_idx = UINT32_MAX, uint32_t rel_block_idx = UINT32_MAX,
             uint32_t inst_idx = UINT32_MAX) {
        issues.push_back(VerifyIssue{code, std::move(msg), func_idx, rel_block_idx, inst_idx});
    }
};
VerifyReport verifyModuleIRFlat(const ModuleIR &module);
} // namespace niki::ir