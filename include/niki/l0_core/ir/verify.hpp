#pragma once

#include "module_ir.hpp"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
namespace niki::ir {

enum class VerifyErrorCode : uint16_t {
    None = 0,
    // 模块级
    InvalidInitializerFunctionIdentifier,
    DuplicateSymbolNameIdentifier,
    // 函数级
    InvalidFunctionIdentifier,
    InvalidEntryBlockIdentifier,
    InvalidBlockIdentifier,
    DuplicateBlockIdentifier,
    // 基本块级
    EmptyBlockWithoutTerminator,
    MissingTerminatorInstruction,
    InvalidTerminatorInstruction,

    // 指令与操作数级
    InvalidInstructionOperandLayout,
    InvalidDestinationValueKind,
    InvalidSourceValueKind,
    InvalidVirtualRegisterIdentifier,
    InvalidFunctionReference,
    InvalidBlockReference,
    InvalidSymbolReference,
    InvalidStringReference,
    // 调用语义
    CallArgumentCountMismatch
};

struct VerifyIssue {
    VerifyErrorCode error_code = VerifyErrorCode::None;
    std::string message;

    // 定位信息(尽量提供，无法定位时保持uint32_max)
    uint32_t function_identifier = UINT32_MAX;
    uint32_t block_identifier = UINT32_MAX;
    uint32_t instruction_index = UINT32_MAX;
};

struct VerifyReport {
    std::vector<VerifyIssue> issues;
    bool ok() const { return issues.empty(); }

    void addIssue(const VerifyIssue &issue) { issues.push_back(issue); }

    void addIssue(VerifyErrorCode error_code, std::string message, uint32_t function_identifier = UINT32_MAX,
                  uint32_t block_identifier = UINT32_MAX, uint32_t instruction_index = UINT32_MAX) {
        issues.push_back(VerifyIssue{
            .error_code = error_code,
            .message = std::move(message),
            .function_identifier = function_identifier,
            .block_identifier = block_identifier,
            .instruction_index = instruction_index,
        });
    }
};

VerifyReport verifyModuleIR(const ModuleIR &module_ir);

VerifyReport verifyFunctionIR(const ModuleIR &module_ir, const IRFunction &function_ir);

bool hasStructuralErrors(const VerifyReport &report);
} // namespace niki::ir