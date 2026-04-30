#include "niki/l0_core/ir/verify.hpp"
#include "niki/l1_domain/validator.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace niki::ir::test {
namespace {

struct DomainVerifyRegistrationGuard {
    DomainVerifyRegistrationGuard() { l1_domain::registerVerifierExtensions(); }
};

DomainVerifyRegistrationGuard g_domain_verify_registration_guard{};

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

TEST(VerifyIRTest, KitsItemSpanOutOfRange_ShouldBeReported) {
    ModuleIR module_ir = makeMinimalValidModule();
    module_ir.kits.push_back(KitsRecord{
        .kits_sid = 0,
        .owner_mod_sid = 0,
        .first_item = 0,
        .item_count = 1,
    });

    VerifyReport report = verifyModuleIRFlat(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::KitsItemSpanOutOfRange));
}

TEST(VerifyIRTest, KitsSidOutOfRange_ShouldBeReported) {
    ModuleIR module_ir = makeMinimalValidModule();
    module_ir.kits.push_back(KitsRecord{
        .kits_sid = 999,
        .owner_mod_sid = 888,
        .first_item = 0,
        .item_count = 0,
    });

    VerifyReport report = verifyModuleIRFlat(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::KitsNameRefOutOfRange));
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::KitsOwnerModuleRefOutOfRange));
}

TEST(VerifyIRTest, KitsItemSidOutOfRange_ShouldBeReported) {
    ModuleIR module_ir = makeMinimalValidModule();
    module_ir.kits_items.push_back(KitsItemRecord{
        .alias_sid = 777,
        .component_sid = 666,
        .is_mutable = true,
    });
    module_ir.kits.push_back(KitsRecord{
        .kits_sid = 0,
        .owner_mod_sid = 0,
        .first_item = 0,
        .item_count = 1,
    });

    VerifyReport report = verifyModuleIRFlat(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::KitsAliasRefOutOfRange));
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::KitsComponentRefOutOfRange));
}

TEST(VerifyIRTest, KitsDuplicateAliasInSameWindow_ShouldBeReported) {
    ModuleIR module_ir = makeMinimalValidModule();
    module_ir.string_pool = {"m", "owner", "pos", "Position", "Velocity"};
    module_ir.kits_items.push_back(KitsItemRecord{
        .alias_sid = 2,
        .component_sid = 3,
        .is_mutable = true,
    });
    module_ir.kits_items.push_back(KitsItemRecord{
        .alias_sid = 2,
        .component_sid = 4,
        .is_mutable = false,
    });
    module_ir.kits.push_back(KitsRecord{
        .kits_sid = 0,
        .owner_mod_sid = 1,
        .first_item = 0,
        .item_count = 2,
    });

    VerifyReport report = verifyModuleIRFlat(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::KitsDuplicateAliasInWindow));
}

TEST(VerifyIRTest, ComponentSidOutOfRange_ShouldBeReported) {
    ModuleIR module_ir = makeMinimalValidModule();
    module_ir.components.push_back(ComponentRecord{
        .component_sid = 999,
        .source_struct_sid = std::numeric_limits<uint32_t>::max(),
        .owner_mod_sid = 888,
        .is_struct_promotion = false,
    });

    VerifyReport report = verifyModuleIRFlat(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::ComponentNameRefOutOfRange));
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::ComponentOwnerModuleRefOutOfRange));
}

TEST(VerifyIRTest, ComponentPromotionSourceStructOutOfRange_ShouldBeReported) {
    ModuleIR module_ir = makeMinimalValidModule();
    module_ir.components.push_back(ComponentRecord{
        .component_sid = 0,
        .source_struct_sid = 777,
        .owner_mod_sid = 0,
        .is_struct_promotion = true,
    });

    VerifyReport report = verifyModuleIRFlat(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::ComponentSourceStructRefOutOfRange));
}

} // namespace
} // namespace niki::ir::test

