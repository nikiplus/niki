#include "niki/l0_core/ir/lower_to_chunk.hpp"

#include <gtest/gtest.h>

namespace niki::ir::test {
namespace {

ModuleIR makeTwoBlockJumpModule() {
    ModuleIR module_ir;
    module_ir.module_name = "lower_jump_test";
    module_ir.module_src_path = "<lower_jump_test>";

    module_ir.funcs.push_back(FuncRecord{
        .func_id = 0,
        .func_name_sid = 0,
        .src_sid = 0,
        .entry_block = 0,
        .next_vreg = 1,
        .block_span = Span{0, 2},
    });

    module_ir.blocks.push_back(BlockRecord{
        .block_id = 0,
        .debug_name_sid = 0,
        .inst_span = Span{0, 1},
    });
    module_ir.blocks.push_back(BlockRecord{
        .block_id = 1,
        .debug_name_sid = 1,
        .inst_span = Span{1, 1},
    });

    module_ir.insts.push(InstKind::Jump, ValueKind::Invalid, 0, 0, 0, ValueKind::BlockId, 1, 0, 0, ValueKind::Invalid,
                         0, 0, 0, ValueKind::Invalid, 0, 0, 0, 0, 1, 1);
    module_ir.insts.push(InstKind::Jump, ValueKind::Invalid, 0, 0, 0, ValueKind::BlockId, 0, 0, 0, ValueKind::Invalid,
                         0, 0, 0, ValueKind::Invalid, 0, 0, 0, 0, 2, 1);
    return module_ir;
}

ModuleIR makeBlockedMemberAccessModule() {
    ModuleIR module_ir;
    module_ir.module_name = "lower_blocked_test";
    module_ir.module_src_path = "<lower_blocked_test>";

    module_ir.funcs.push_back(FuncRecord{
        .func_id = 0,
        .func_name_sid = 0,
        .src_sid = 0,
        .entry_block = 0,
        .next_vreg = 3,
        .block_span = Span{0, 1},
    });

    module_ir.blocks.push_back(BlockRecord{
        .block_id = 0,
        .debug_name_sid = 0,
        .inst_span = Span{0, 1},
    });

    module_ir.insts.push(InstKind::GetMember, ValueKind::VReg, 0, 0, 0, ValueKind::VReg, 1, 0, 0, ValueKind::VReg, 2, 0,
                         0, ValueKind::Invalid, 0, 0, 0, 0, 1, 1);
    return module_ir;
}

TEST(LowerToChunkTest, BackwardJumpShouldBePatchedToLoopOpcode) {
    ModuleIR module_ir = makeTwoBlockJumpModule();
    auto lowered = lowerFunctionToChunk(module_ir, 0);
    ASSERT_TRUE(lowered.has_value()) << lowered.error();

    const auto &code = lowered.value()->chunk.code;
    ASSERT_GE(code.size(), 6u);
    EXPECT_EQ(code[0], vm::ToInt(vm::OPCODE::OP_JMP));
    EXPECT_EQ(code[3], vm::ToInt(vm::OPCODE::OP_LOOP));
}

TEST(LowerToChunkTest, InstOpcodeChecklistShouldCoverAllInstKinds) {
    const auto &checklist = instOpcodeChecklist();
    EXPECT_EQ(checklist.size(), static_cast<size_t>(InstKind::Phi) + 1u);
    EXPECT_EQ(checklist.front().inst_kind, InstKind::Nop);
    EXPECT_EQ(checklist.back().inst_kind, InstKind::Phi);
}

TEST(LowerToChunkTest, BlockedOpcodeMustFailInLoweringStage) {
    ModuleIR module_ir = makeBlockedMemberAccessModule();
    auto lowered = lowerFunctionToChunk(module_ir, 0);
    ASSERT_FALSE(lowered.has_value());
    EXPECT_NE(lowered.error().find("opcode not runnable"), std::string::npos);
    EXPECT_NE(lowered.error().find("OP_GET_PROPERTY"), std::string::npos);
}

} // namespace
} // namespace niki::ir::test
