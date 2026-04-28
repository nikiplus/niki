#include "niki/l0_core/ir/verify.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

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
    module_ir.module_string_pool = {"member_name"};

    IRFunction &function_ir = module_ir.createFunc(0, "<verify_test>");
    function_ir.func_sig.return_type = IRType::makeUnknown();
    IRBasicBlock &entry_block = function_ir.createBasicBlock("entry");
    function_ir.entry_block_id = entry_block.block_id;
    entry_block.instruction_list.push_back(
        IRInst{.instruction_kind = IRInstKind::Return, .first_operand = IRValue::makeInvalid()});

    return module_ir;
}

TEST(VerifyIRTest, MinimalValidModule_ShouldPass) {
    ModuleIR module_ir = makeMinimalValidModule();
    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(report.ok());
    EXPECT_FALSE(hasStructuralErrors(report));
}

TEST(VerifyIRTest, InvalidStringReference_ShouldBeReportedAsStructural) {
    ModuleIR module_ir = makeMinimalValidModule();
    IRFunction &function_ir = module_ir.func_table.front();
    IRBasicBlock &entry_block = function_ir.basic_blocks.front();

    IRRegId dst = function_ir.allocateVirtualRegister();
    entry_block.instruction_list.insert(
        entry_block.instruction_list.begin(),
        IRInst{.instruction_kind = IRInstKind::Constant,
               .destination_value = IRValue::makeVirtualRegisterValue(dst),
               .first_operand = IRValue::makeStringIdentifierValue(9999)});

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::InvalidStringReference));
    EXPECT_TRUE(hasStructuralErrors(report));
}

TEST(VerifyIRTest, TerminatorNotLast_ShouldBeReportedAsStructural) {
    ModuleIR module_ir = makeMinimalValidModule();
    IRFunction &function_ir = module_ir.func_table.front();
    IRBasicBlock &entry_block = function_ir.basic_blocks.front();

    entry_block.instruction_list.clear();
    entry_block.instruction_list.push_back(IRInst{.instruction_kind = IRInstKind::Return});
    IRRegId dst = function_ir.allocateVirtualRegister();
    entry_block.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::Constant,
        .destination_value = IRValue::makeVirtualRegisterValue(dst),
        .first_operand = IRValue::makeImmediateIntegerValue(1),
    });

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::InvalidTerminatorInstruction));
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::MissingTerminatorInstruction));
    EXPECT_TRUE(hasStructuralErrors(report));
}

TEST(VerifyIRTest, DuplicateSymbolName_ShouldBeNonStructural) {
    ModuleIR module_ir = makeMinimalValidModule();
    module_ir.sym_table.push_back(IRSymbol{.sym_id = 0, .sym_name_id = 42, .sym_kind = IRSymbolKind::Function});
    module_ir.sym_table.push_back(IRSymbol{.sym_id = 1, .sym_name_id = 42, .sym_kind = IRSymbolKind::GlobalVar});

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::DuplicateSymbolNameIdentifier));
    EXPECT_FALSE(hasStructuralErrors(report));
}

TEST(VerifyIRTest, CallArgMismatch_ShouldBeNonStructural) {
    ModuleIR module_ir;
    module_ir.module_name = "call_mismatch";
    module_ir.module_src_path = "<verify_test>";

    IRFunctionId callee_id = module_ir.createFunc(1, "<verify_test>").func_id;
    IRFunction &callee = module_ir.func_table[callee_id];
    callee.func_sig.parameter_types = {IRType::makeUnknown(), IRType::makeUnknown()};
    IRBasicBlock &callee_entry = callee.createBasicBlock("entry");
    callee.entry_block_id = callee_entry.block_id;
    callee_entry.instruction_list.push_back(IRInst{.instruction_kind = IRInstKind::Return});

    IRFunctionId caller_id = module_ir.createFunc(2, "<verify_test>").func_id;
    IRFunction &caller = module_ir.func_table[caller_id];
    IRBasicBlock &caller_entry = caller.createBasicBlock("entry");
    caller.entry_block_id = caller_entry.block_id;
    IRRegId arg_base_reg = caller.allocateVirtualRegister();
    IRRegId ret_reg = caller.allocateVirtualRegister();
    caller_entry.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::Call,
        .destination_value = IRValue::makeVirtualRegisterValue(ret_reg),
        .first_operand = IRValue::makeFunctionIdentifierValue(callee_id),
        .second_operand = IRValue::makeVirtualRegisterValue(arg_base_reg),
        .auxiliary_data = 1, // expected 2
    });
    caller_entry.instruction_list.push_back(IRInst{.instruction_kind = IRInstKind::Return});

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::CallArgumentCountMismatch));
    EXPECT_FALSE(hasStructuralErrors(report));
}

TEST(VerifyIRTest, InvalidDestinationValueKind_ShouldBeStructural) {
    ModuleIR module_ir = makeMinimalValidModule();
    IRFunction &function_ir = module_ir.func_table.front();
    IRBasicBlock &entry_block = function_ir.basic_blocks.front();

    entry_block.instruction_list.clear();
    function_ir.allocateVirtualRegister();
    entry_block.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::Add,
        .destination_value = IRValue::makeStringIdentifierValue(0), // illegal destination kind
        .first_operand = IRValue::makeVirtualRegisterValue(0),
        .second_operand = IRValue::makeImmediateIntegerValue(1),
    });
    entry_block.instruction_list.push_back(IRInst{.instruction_kind = IRInstKind::Return});

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::InvalidDestinationValueKind));
    EXPECT_TRUE(hasStructuralErrors(report));
}

TEST(VerifyIRTest, InvalidVirtualRegisterIdentifier_ShouldBeStructural) {
    ModuleIR module_ir = makeMinimalValidModule();
    IRFunction &function_ir = module_ir.func_table.front();
    IRBasicBlock &entry_block = function_ir.basic_blocks.front();

    entry_block.instruction_list.clear();
    IRRegId valid_dst = function_ir.allocateVirtualRegister();
    entry_block.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::Move,
        .destination_value = IRValue::makeVirtualRegisterValue(valid_dst),
        .first_operand = IRValue::makeVirtualRegisterValue(valid_dst + 100), // out of range
    });
    entry_block.instruction_list.push_back(IRInst{.instruction_kind = IRInstKind::Return});

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::InvalidVirtualRegisterIdentifier));
    EXPECT_TRUE(hasStructuralErrors(report));
}

TEST(VerifyIRTest, InvalidFunctionReference_ShouldBeStructural) {
    ModuleIR module_ir = makeMinimalValidModule();
    IRFunction &function_ir = module_ir.func_table.front();
    IRBasicBlock &entry_block = function_ir.basic_blocks.front();

    entry_block.instruction_list.clear();
    IRRegId call_dst = function_ir.allocateVirtualRegister();
    entry_block.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::Call,
        .destination_value = IRValue::makeVirtualRegisterValue(call_dst),
        .first_operand = IRValue::makeFunctionIdentifierValue(999), // out of module func_table
        .auxiliary_data = 0,
    });
    entry_block.instruction_list.push_back(IRInst{.instruction_kind = IRInstKind::Return});

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::InvalidFunctionReference));
    EXPECT_TRUE(hasStructuralErrors(report));
}

TEST(VerifyIRTest, InvalidEntryAndBlockIdentifiers_ShouldBeStructural) {
    ModuleIR module_ir = makeMinimalValidModule();
    IRFunction &function_ir = module_ir.func_table.front();
    function_ir.entry_block_id = 99;
    function_ir.basic_blocks.front().block_id = 99;

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::InvalidEntryBlockIdentifier));
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::InvalidBlockIdentifier));
    EXPECT_TRUE(hasStructuralErrors(report));
}

TEST(VerifyIRTest, DuplicateBlockIdentifiers_ShouldBeStructural) {
    ModuleIR module_ir = makeMinimalValidModule();
    IRFunction &function_ir = module_ir.func_table.front();
    IRBasicBlock &second_block = function_ir.createBasicBlock("second");
    second_block.block_id = function_ir.basic_blocks.front().block_id;
    second_block.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::Jump,
        .first_operand = IRValue::makeBlockIdentifierValue(function_ir.entry_block_id),
    });

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::DuplicateBlockIdentifier));
    EXPECT_TRUE(hasStructuralErrors(report));
}

TEST(VerifyIRTest, EmptyBlockWithoutTerminator_ShouldBeStructural) {
    ModuleIR module_ir = makeMinimalValidModule();
    IRFunction &function_ir = module_ir.func_table.front();
    IRBasicBlock &empty_block = function_ir.createBasicBlock("empty");
    (void)empty_block;

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::EmptyBlockWithoutTerminator));
    EXPECT_TRUE(hasStructuralErrors(report));
}

TEST(VerifyIRTest, InvalidBlockAndSymbolReference_ShouldBeStructural) {
    ModuleIR module_ir = makeMinimalValidModule();
    IRFunction &function_ir = module_ir.func_table.front();
    IRBasicBlock &entry_block = function_ir.basic_blocks.front();
    entry_block.instruction_list.clear();

    IRRegId dst = function_ir.allocateVirtualRegister();
    entry_block.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::LoadGlobal,
        .destination_value = IRValue::makeVirtualRegisterValue(dst),
        .first_operand = IRValue::makeSymbolIdentifierValue(999),
    });
    entry_block.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::Jump,
        .first_operand = IRValue::makeBlockIdentifierValue(888),
    });

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::InvalidSymbolReference));
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::InvalidBlockReference));
    EXPECT_TRUE(hasStructuralErrors(report));
}

TEST(VerifyIRTest, InvalidInstructionOperandKinds_ShouldBeStructural) {
    ModuleIR module_ir = makeMinimalValidModule();
    IRFunction &function_ir = module_ir.func_table.front();
    IRBasicBlock &entry_block = function_ir.basic_blocks.front();
    entry_block.instruction_list.clear();

    IRRegId dst0 = function_ir.allocateVirtualRegister();
    IRRegId dst1 = function_ir.allocateVirtualRegister();
    IRRegId dst2 = function_ir.allocateVirtualRegister();
    IRRegId dst3 = function_ir.allocateVirtualRegister();

    entry_block.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::LoadGlobal,
        .destination_value = IRValue::makeVirtualRegisterValue(dst0),
        .first_operand = IRValue::makeImmediateIntegerValue(1), // should be SymbolId
    });
    entry_block.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::GetMember,
        .destination_value = IRValue::makeVirtualRegisterValue(dst1),
        .first_operand = IRValue::makeVirtualRegisterValue(dst0),
        .second_operand = IRValue::makeImmediateIntegerValue(2), // should be StringId
    });
    entry_block.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::SetMember,
        .destination_value = IRValue::makeInvalid(),
        .first_operand = IRValue::makeVirtualRegisterValue(dst0),
        .second_operand = IRValue::makeImmediateIntegerValue(2), // should be StringId
        .third_operand = IRValue::makeVirtualRegisterValue(dst1),
    });
    entry_block.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::Call,
        .destination_value = IRValue::makeVirtualRegisterValue(dst2),
        .first_operand = IRValue::makeSymbolIdentifierValue(0), // only VReg/FuncId is allowed
        .second_operand = IRValue::makeImmediateIntegerValue(0),
        .auxiliary_data = 1,
    });
    entry_block.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::Return,
        .first_operand = IRValue::makeVirtualRegisterValue(dst3),
    });

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(hasIssueCode(report, VerifyErrorCode::InvalidSourceValueKind));
    EXPECT_TRUE(hasStructuralErrors(report));
}

} // namespace
} // namespace niki::ir::test

