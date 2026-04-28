#include "niki/driver/driver.hpp"
#include "niki/l0_core/diagnostic/renderer.hpp"
#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/ir/verify.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace niki::ir::test {
namespace {

class IRBuilderTest : public ::testing::Test {
  protected:
    syntax::GlobalInterner interner;
    GlobalSymbolTable global_symbols;
    GlobalTypeArena global_arena;

    std::expected<ModuleIR, diagnostic::DiagnosticBag> buildModule(std::string_view source) {
        GlobalCompilationUnit unit(interner);
        unit.source_path = "<ir_builder_test>";
        unit.source = std::string(source);

        auto parse_result = driver::parseIntoCompilationUnit(unit);
        if (!parse_result.has_value()) {
            return std::unexpected(std::move(parse_result.error()));
        }

        IRBuilder builder;
        return builder.build(unit, &global_symbols, &global_arena);
    }

    static size_t countInstKind(const IRFunction &function_ir, IRInstKind instruction_kind) {
        size_t count = 0;
        for (const IRBasicBlock &block : function_ir.basic_blocks) {
            for (const IRInst &inst : block.instruction_list) {
                if (inst.instruction_kind == instruction_kind) {
                    ++count;
                }
            }
        }
        return count;
    }

    static std::vector<const IRInst *> collectInstKind(const IRFunction &function_ir, IRInstKind instruction_kind) {
        std::vector<const IRInst *> instructions;
        for (const IRBasicBlock &block : function_ir.basic_blocks) {
            for (const IRInst &inst : block.instruction_list) {
                if (inst.instruction_kind == instruction_kind) {
                    instructions.push_back(&inst);
                }
            }
        }
        return instructions;
    }

    static std::optional<const IRInst *> findFirstInstKind(const IRFunction &function_ir, IRInstKind instruction_kind) {
        for (const IRBasicBlock &block : function_ir.basic_blocks) {
            for (const IRInst &inst : block.instruction_list) {
                if (inst.instruction_kind == instruction_kind) {
                    return &inst;
                }
            }
        }
        return std::nullopt;
    }
};

TEST_F(IRBuilderTest, BuildArrayMapAndIndexMemberReadWrite_ShouldEmitExpectedInstructions) {
    auto result = buildModule(R"(
func test() {
    var arr = [1, 2, 3];
    var dict = {"x": 10, "y": 20};
    var idx_val = arr[1];
    var mem_val = arr.head;
    arr[2] = idx_val;
    arr[2] += 7;
    arr.head = mem_val;
    arr.head += 3;
    return arr.head;
}
)");
    ASSERT_TRUE(result.has_value())
        << "IR builder failed for array/map/index/member sample.\n"
        << diagnostic::renderDiagnosticBagText(result.error());

    const ModuleIR &module_ir = result.value();
    ASSERT_FALSE(module_ir.func_table.empty());
    const IRFunction &function_ir = module_ir.func_table.front();

    EXPECT_GE(countInstKind(function_ir, IRInstKind::NewArray), 1u);
    EXPECT_GE(countInstKind(function_ir, IRInstKind::PushArray), 3u);
    EXPECT_GE(countInstKind(function_ir, IRInstKind::NewMap), 1u);
    EXPECT_GE(countInstKind(function_ir, IRInstKind::SetMap), 2u);
    EXPECT_GE(countInstKind(function_ir, IRInstKind::Constant), 6u);

    EXPECT_GE(countInstKind(function_ir, IRInstKind::GetIndex), 2u);
    EXPECT_GE(countInstKind(function_ir, IRInstKind::SetIndex), 2u);
    EXPECT_GE(countInstKind(function_ir, IRInstKind::GetMember), 2u);
    EXPECT_GE(countInstKind(function_ir, IRInstKind::SetMember), 2u);
    EXPECT_GE(countInstKind(function_ir, IRInstKind::Add), 2u);

    // 精确断言：字符串字面量必须被降为 Constant + StringId
    auto constant_instructions = collectInstKind(function_ir, IRInstKind::Constant);
    bool has_string_literal_constant = false;
    for (const IRInst *inst : constant_instructions) {
        if (inst->destination_value.value_kind == IRValueKind::VReg &&
            inst->first_operand.value_kind == IRValueKind::StringId) {
            has_string_literal_constant = true;
            break;
        }
    }
    EXPECT_TRUE(has_string_literal_constant) << "Expected at least one Constant instruction carrying StringId.";

    // 精确断言：Get/SetIndex 操作数形状
    auto get_index_instructions = collectInstKind(function_ir, IRInstKind::GetIndex);
    ASSERT_FALSE(get_index_instructions.empty());
    for (const IRInst *inst : get_index_instructions) {
        EXPECT_EQ(inst->destination_value.value_kind, IRValueKind::VReg);
        EXPECT_EQ(inst->first_operand.value_kind, IRValueKind::VReg);  // target
        EXPECT_EQ(inst->second_operand.value_kind, IRValueKind::VReg); // index
    }

    auto set_index_instructions = collectInstKind(function_ir, IRInstKind::SetIndex);
    ASSERT_FALSE(set_index_instructions.empty());
    for (const IRInst *inst : set_index_instructions) {
        EXPECT_EQ(inst->destination_value.value_kind, IRValueKind::Invalid);
        EXPECT_EQ(inst->first_operand.value_kind, IRValueKind::VReg);  // target
        EXPECT_EQ(inst->second_operand.value_kind, IRValueKind::VReg); // index
        EXPECT_EQ(inst->third_operand.value_kind, IRValueKind::VReg);  // value
    }

    // 精确断言：Get/SetMember 操作数形状（member 名称走 StringId）
    auto get_member_instructions = collectInstKind(function_ir, IRInstKind::GetMember);
    ASSERT_FALSE(get_member_instructions.empty());
    for (const IRInst *inst : get_member_instructions) {
        EXPECT_EQ(inst->destination_value.value_kind, IRValueKind::VReg);
        EXPECT_EQ(inst->first_operand.value_kind, IRValueKind::VReg);     // object
        EXPECT_EQ(inst->second_operand.value_kind, IRValueKind::StringId); // property id
    }

    auto set_member_instructions = collectInstKind(function_ir, IRInstKind::SetMember);
    ASSERT_FALSE(set_member_instructions.empty());
    for (const IRInst *inst : set_member_instructions) {
        EXPECT_EQ(inst->destination_value.value_kind, IRValueKind::Invalid);
        EXPECT_EQ(inst->first_operand.value_kind, IRValueKind::VReg);      // object
        EXPECT_EQ(inst->second_operand.value_kind, IRValueKind::StringId); // property id
        EXPECT_EQ(inst->third_operand.value_kind, IRValueKind::VReg);      // value
    }

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(report.ok()) << "IR verify failed after builder output.";
}

TEST_F(IRBuilderTest, BuildIndexAndMemberCompoundAssign_ShouldKeepStructuralValidity) {
    auto result = buildModule(R"(
func test() {
    var arr = [4, 5];
    var box = {1: 8};
    arr[0] *= 2;
    arr.value %= 3;
    return arr[0];
}
)");
    ASSERT_TRUE(result.has_value())
        << "IR builder failed for compound assignment sample.\n"
        << diagnostic::renderDiagnosticBagText(result.error());

    const ModuleIR &module_ir = result.value();
    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(report.ok()) << "IR verify failed for index/member compound assignment output.";

    ASSERT_FALSE(module_ir.func_table.empty());
    const IRFunction &function_ir = module_ir.func_table.front();
    EXPECT_GE(countInstKind(function_ir, IRInstKind::Mul), 1u);
    EXPECT_GE(countInstKind(function_ir, IRInstKind::Mod), 1u);
    EXPECT_GE(countInstKind(function_ir, IRInstKind::SetIndex), 1u);
    EXPECT_GE(countInstKind(function_ir, IRInstKind::SetMember), 1u);
}

TEST_F(IRBuilderTest, BuildStringLiteralConstant_StringIdMustBeInModulePoolRange) {
    auto result = buildModule(R"(
func test() {
    var greeting = "hello_ir";
    return greeting;
}
)");
    ASSERT_TRUE(result.has_value()) << diagnostic::renderDiagnosticBagText(result.error());

    const ModuleIR &module_ir = result.value();
    ASSERT_FALSE(module_ir.func_table.empty());
    const IRFunction &function_ir = module_ir.func_table.front();
    auto constant_instructions = collectInstKind(function_ir, IRInstKind::Constant);
    ASSERT_FALSE(constant_instructions.empty());

    bool found_string_constant = false;
    for (const IRInst *inst : constant_instructions) {
        if (inst->first_operand.value_kind != IRValueKind::StringId) {
            continue;
        }
        found_string_constant = true;
        EXPECT_LT(static_cast<size_t>(inst->first_operand.payload_as_u32), module_ir.module_string_pool.size());
        EXPECT_EQ(module_ir.module_string_pool[inst->first_operand.payload_as_u32], "hello_ir");
    }
    EXPECT_TRUE(found_string_constant) << "Expected Constant(StringId) for string literal.";
}

TEST_F(IRBuilderTest, BuildLoopBreakContinue_ShouldEmitValidTerminatedCFG) {
    auto result = buildModule(R"(
func test(limit) {
    var i = 0;
    loop (i < limit) {
        i += 1;
        if (i == 2) {
            continue;
        }
        if (i == 4) {
            break;
        }
    }
    return i;
}
)");
    ASSERT_TRUE(result.has_value()) << diagnostic::renderDiagnosticBagText(result.error());
    const ModuleIR &module_ir = result.value();
    ASSERT_FALSE(module_ir.func_table.empty());
    const IRFunction &function_ir = module_ir.func_table.front();

    EXPECT_GE(countInstKind(function_ir, IRInstKind::Branch), 2u);
    EXPECT_GE(countInstKind(function_ir, IRInstKind::Jump), 3u);

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(report.ok()) << "IR verify failed for loop break/continue case.";
}

TEST_F(IRBuilderTest, BuildCallExpr_ShouldUseContiguousArgumentWindowEncoding) {
    auto result = buildModule(R"(
func test() {
    var a = 1;
    var b = 2;
    return callee(a + 1, b * 2);
}
)");
    ASSERT_TRUE(result.has_value()) << diagnostic::renderDiagnosticBagText(result.error());
    const ModuleIR &module_ir = result.value();
    ASSERT_FALSE(module_ir.func_table.empty());
    const IRFunction &function_ir = module_ir.func_table.front();

    auto call_inst_opt = findFirstInstKind(function_ir, IRInstKind::Call);
    ASSERT_TRUE(call_inst_opt.has_value());
    const IRInst *call_inst = call_inst_opt.value();

    ASSERT_EQ(call_inst->first_operand.value_kind, IRValueKind::VReg);
    ASSERT_EQ(call_inst->auxiliary_data, 2u);
    ASSERT_EQ(call_inst->second_operand.value_kind, IRValueKind::VReg);

    const IRRegId arg_base = call_inst->second_operand.payload_as_u32;
    bool has_arg0_move = false;
    bool has_arg1_move = false;
    for (const IRBasicBlock &block : function_ir.basic_blocks) {
        for (const IRInst &inst : block.instruction_list) {
            if (inst.instruction_kind != IRInstKind::Move || inst.destination_value.value_kind != IRValueKind::VReg) {
                continue;
            }
            if (inst.destination_value.payload_as_u32 == arg_base) {
                has_arg0_move = true;
            }
            if (inst.destination_value.payload_as_u32 == arg_base + 1) {
                has_arg1_move = true;
            }
        }
    }
    EXPECT_TRUE(has_arg0_move);
    EXPECT_TRUE(has_arg1_move);

    VerifyReport report = verifyModuleIR(module_ir);
    EXPECT_TRUE(report.ok()) << "IR verify failed for call encoding case.";
}

TEST_F(IRBuilderTest, BreakContinueOutsideLoop_ShouldReportDiagnostics) {
    auto break_result = buildModule(R"(
func test() {
    break;
}
)");
    ASSERT_FALSE(break_result.has_value());
    EXPECT_FALSE(break_result.error().empty());

    auto continue_result = buildModule(R"(
func test() {
    continue;
}
)");
    ASSERT_FALSE(continue_result.has_value());
    EXPECT_FALSE(continue_result.error().empty());
}

} // namespace
} // namespace niki::ir::test

