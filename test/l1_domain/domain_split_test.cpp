#include "niki/l1_domain/validator.hpp"
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/ir/verify.hpp"
#include <gtest/gtest.h>

namespace niki::l1_domain::test {
namespace {

TEST(L1DomainValidatorTest, DomainCheckShouldReportKitsSpanOutOfRange) {
    ir::ModuleIR module_ir;
    module_ir.string_pool = {"mod", "MoveWindow"};
    module_ir.kits.push_back(ir::KitsRecord{
        .kits_sid = 1,
        .owner_mod_sid = 0,
        .first_item = 0,
        .item_count = 1,
    });

    ir::VerifyReport report;
    appendDomainIRChecks(module_ir, report);

    bool found = false;
    for (const auto &issue : report.issues) {
        if (issue.error_code == ir::VerifyErrorCode::KitsItemSpanOutOfRange) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

} // namespace
} // namespace niki::l1_domain::test

