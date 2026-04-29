#include "niki/l0_core/ir/verify.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace niki::ir::test {
namespace {

bool hasIssueCode(const VerifyReport &report, VerifyErrorCode code) {
    return std::any_of(report.issues.begin(), report.issues.end(),
                       [code](const VerifyIssue &issue) { return issue.error_code == code; });
}

ModuleIR makeMinimalValidModule() {
    ModuleIR module_ir;
    module_ir.module_name = "verify_test_module";
    module_ir.module_src_path = "<verify_test>";
    module_ir.string_pool = {"member_name"};

    module_ir.funcs.push_back(FuncRecord{
        .func_id = 0,
        .func_name_sid = 0,
        .src_sid = 0,
        .entry_block = 0,
        .next_vreg = 1,
        .block_span = Span{0, 1},
    });
    module_ir.blocks.push_back(BlockRecord{
        .block_id = 0,
        .debug_name_sid = 0,
        .inst_span = Span{0, 1},
    });
    module_ir.insts.push(InstKind::Return, ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid,
                         0, 0, 0, ValueKind::Invalid, 0, 0, 0, 0, 1, 1);

    return module_ir;
}

TEST(VerifyIRTest, MinimalValidModule_ShouldPass) {
    ModuleIR module_ir = makeMinimalValidModule();
    VerifyReport report = verifyModuleIRFlat(module_ir);
    EXPECT_TRUE(report.ok());
}

TEST(VerifyIRTest, StringReferenceOutOfRange_ShouldBeReported) {
    ModuleIR module_ir = makeMinimalValidModule();
    module_ir.insts.kind[0] = InstKind::Constant;
    module_ir.insts.dst_kind[0] = ValueKind::VReg;
    module_ir.insts.dst_u32[0] = 0;
    module_ir.insts.a_kind[0] = ValueKind::StringId;
    module_ir.insts.a_u32[0] = 9999;

    VerifyReport report = verifyModuleIRFlat(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::StringRefOutOfRange));
}

TEST(VerifyIRTest, TerminatorNotLastAndMissingTerminator_ShouldBeReported) {
    ModuleIR module_ir = makeMinimalValidModule();
    module_ir.blocks[0].inst_span.count = 2;
    module_ir.insts.push(InstKind::Constant, ValueKind::VReg, 0, 0, 0, ValueKind::ImmI64, 0, 1, 0, ValueKind::Invalid, 0,
                         0, 0, ValueKind::Invalid, 0, 0, 0, 0, 1, 2);

    VerifyReport report = verifyModuleIRFlat(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::TerminatorNotLast));
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::MissingTerminator));
}

TEST(VerifyIRTest, EntryAndBlockReferenceOutOfRange_ShouldBeReported) {
    ModuleIR module_ir = makeMinimalValidModule();
    module_ir.funcs[0].entry_block = 99;
    module_ir.insts.kind[0] = InstKind::Jump;
    module_ir.insts.a_kind[0] = ValueKind::BlockId;
    module_ir.insts.a_u32[0] = 9;

    VerifyReport report = verifyModuleIRFlat(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::EntryBlockOutOfRange));
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::BlockRefOutOfRange));
}

TEST(VerifyIRTest, VRegAndFuncAndSymbolOutOfRange_ShouldBeReported) {
    ModuleIR module_ir = makeMinimalValidModule();
    module_ir.insts.kind[0] = InstKind::Call;
    module_ir.insts.dst_kind[0] = ValueKind::VReg;
    module_ir.insts.dst_u32[0] = 100;
    module_ir.insts.a_kind[0] = ValueKind::FuncId;
    module_ir.insts.a_u32[0] = 3;
    module_ir.insts.b_kind[0] = ValueKind::SymbolId;
    module_ir.insts.b_u32[0] = 4;

    VerifyReport report = verifyModuleIRFlat(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::VRegOutOfRange));
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::FuncRefOutOfRange));
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::SymbolRefOutOfRange));
}

} // namespace
} // namespace niki::ir::test

