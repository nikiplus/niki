#pragma once
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/vm/object.hpp"
#include <expected>
#include <string>
#include <vector>
namespace niki::ir {
// RESULT: 降解函数结果，包含函数对象与可选错误信息。
struct LowerResult {
    std::vector<vm::ObjFunction *> functions;
};
// API: 模块级降解入口（MVP）。
// NOTE: 当前实现按 ModuleIR.funcs 顺序生成 ObjFunction 列表。
std::expected<LowerResult, std::string> lowerModuleToChunk(const ModuleIR &module_ir);
// API: 单函数降解入口（便于调试）。
std::expected<vm::ObjFunction *, std::string> lowerFunctionToChunk(const ModuleIR &module_ir, FuncId function_id);
} // namespace niki::ir
