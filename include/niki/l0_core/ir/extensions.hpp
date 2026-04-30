#pragma once
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/ir/verify.hpp"

namespace niki::ir {
using DomainVerifyAppendFn = void (*)(const ModuleIR &module_ir, VerifyReport &report);

void registerDomainVerifyAppendFn(DomainVerifyAppendFn fn);
DomainVerifyAppendFn getDomainVerifyAppendFn();
} // namespace niki::ir

