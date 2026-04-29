#include "niki/l0_core/ir/module_ir.hpp"

#include <gtest/gtest.h>

namespace niki::ir::test {
namespace {

TEST(ModuleIRTest, InternShouldDeduplicateAndPreserveOrder) {
    ModuleIR module_ir;

    const uint32_t alpha0 = module_ir.intern("alpha");
    const uint32_t beta = module_ir.intern("beta");
    const uint32_t alpha1 = module_ir.intern("alpha");

    EXPECT_EQ(alpha0, 0u);
    EXPECT_EQ(beta, 1u);
    EXPECT_EQ(alpha1, alpha0);
    ASSERT_EQ(module_ir.string_pool.size(), 2u);
    EXPECT_EQ(module_ir.string_pool[0], "alpha");
    EXPECT_EQ(module_ir.string_pool[1], "beta");
}

TEST(ModuleIRTest, InstTablePushShouldKeepColumnsAligned) {
    InstTable table;
    const InstId inst_id = table.push(InstKind::Constant, ValueKind::VReg, 0, 0, 0, ValueKind::ImmI64, 0, 42, 0,
                                      ValueKind::Invalid, 0, 0, 0, ValueKind::Invalid, 0, 0, 0, 7, 12, 34);

    EXPECT_EQ(inst_id, 0u);
    EXPECT_EQ(table.size(), 1u);
    EXPECT_TRUE(table.aligned());
    EXPECT_EQ(table.kind[0], InstKind::Constant);
    EXPECT_EQ(table.dst_kind[0], ValueKind::VReg);
    EXPECT_EQ(table.a_kind[0], ValueKind::ImmI64);
    EXPECT_EQ(table.a_i64[0], 42);
    EXPECT_EQ(table.aux[0], 7u);
    EXPECT_EQ(table.src_line[0], 12u);
    EXPECT_EQ(table.src_col[0], 34u);
}

TEST(ModuleIRTest, HasInitShouldReflectInitFuncSentinel) {
    ModuleIR module_ir;
    EXPECT_FALSE(module_ir.has_init());

    module_ir.init_func = 0;
    EXPECT_TRUE(module_ir.has_init());
}

} // namespace
} // namespace niki::ir::test

