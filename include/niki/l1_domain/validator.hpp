#pragma once
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/ir/verify.hpp"

namespace niki::l1_domain {
void appendDomainIRChecks(const ir::ModuleIR &module_ir, ir::VerifyReport &report);
void registerVerifierExtensions();
} // namespace niki::l1_domain

