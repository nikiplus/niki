#include "niki/l0_core/ir/module_ir.hpp"

#include <gtest/gtest.h>

#include <string>

namespace niki::ir::test {
namespace {

TEST(ModuleIRTest, CreateFuncAndFindHelpers_ShouldBeConsistent) {
    ModuleIR module_ir;
    module_ir.module_name = "module_ir_test";
    module_ir.module_src_path = "<module_ir_test>";

    IRFunctionId first_function_id = module_ir.createFunc(1001, "a.nk").func_id;
    IRFunctionId second_function_id = module_ir.createFunc(1002, "b.nk").func_id;
    IRFunction &first_function = module_ir.func_table[first_function_id];
    IRFunction &second_function = module_ir.func_table[second_function_id];
    IRBasicBlock &entry_block = second_function.createBasicBlock("entry");
    second_function.entry_block_id = entry_block.block_id;

    ASSERT_EQ(first_function.func_id, 0u);
    ASSERT_EQ(second_function.func_id, 1u);

    const IRFunction *found_second_const = findFuncById(module_ir, 1);
    ASSERT_NE(found_second_const, nullptr);
    EXPECT_EQ(found_second_const->func_name_id, 1002u);

    IRFunction *found_second_mut = findFuncById(module_ir, 1);
    ASSERT_NE(found_second_mut, nullptr);
    EXPECT_EQ(found_second_mut->func_src_path, "b.nk");

    const IRBasicBlock *found_entry = findBlockById(*found_second_const, 0);
    ASSERT_NE(found_entry, nullptr);
    EXPECT_EQ(found_entry->debug_block_name, "entry");
}

TEST(ModuleIRTest, AddSymShouldDeduplicateByNameId) {
    ModuleIR module_ir;
    IRSymbolId first_symbol_id =
        module_ir.addSym(42, IRSymbolKind::Function, IRType::makeUnknown(), "mod_a", true);
    IRSymbolId second_symbol_id =
        module_ir.addSym(42, IRSymbolKind::Struct, IRType::makeUnknown(), "mod_b", false);

    EXPECT_EQ(first_symbol_id, second_symbol_id);
    ASSERT_EQ(module_ir.sym_table.size(), 1u);
    EXPECT_EQ(module_ir.sym_table.front().sym_kind, IRSymbolKind::Function);
    EXPECT_EQ(module_ir.sym_table.front().owner_mod_path, "mod_a");
}

TEST(ModuleIRTest, DumpAndFormatShouldContainReadableMarkers) {
    ModuleIR module_ir;
    module_ir.module_name = "dump_mod";
    module_ir.module_src_path = "dump.nk";
    module_ir.module_string_pool = {"hello"};

    IRFunction &function_ir = module_ir.createFunc(1, "dump.nk");
    IRBasicBlock &entry_block = function_ir.createBasicBlock("entry");
    function_ir.entry_block_id = entry_block.block_id;
    IRRegId dst = function_ir.allocateVirtualRegister();
    entry_block.instruction_list.push_back(IRInst{
        .instruction_kind = IRInstKind::Constant,
        .destination_value = IRValue::makeVirtualRegisterValue(dst),
        .first_operand = IRValue::makeStringIdentifierValue(0),
        .auxiliary_data = 7,
    });
    entry_block.instruction_list.push_back(IRInst{.instruction_kind = IRInstKind::Return});

    std::string rendered_value = formatValue(IRValue::makeStringIdentifierValue(0));
    EXPECT_EQ(rendered_value, "str#0");

    std::string rendered_instruction = dumpInstruction(entry_block.instruction_list.front(), 0);
    EXPECT_NE(rendered_instruction.find("Constant"), std::string::npos);
    EXPECT_NE(rendered_instruction.find("str#0"), std::string::npos);
    EXPECT_NE(rendered_instruction.find("aux= 7"), std::string::npos);

    std::string rendered_module = dumpModule(module_ir);
    EXPECT_NE(rendered_module.find("ModuleIR \"dump_mod\""), std::string::npos);
    EXPECT_NE(rendered_module.find("function_count: 1"), std::string::npos);
    EXPECT_NE(rendered_module.find("block #0\"entry\""), std::string::npos);
}

} // namespace
} // namespace niki::ir::test

