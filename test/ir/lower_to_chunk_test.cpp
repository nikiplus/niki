#include "../test_helpers.hpp"
#include "niki/l0_core/ir/lower_to_chunk.hpp"
#include "niki/l0_core/ir/verify.hpp"
#include <gtest/gtest.h>

using namespace niki::ir;

/** @lower_test: IR 降级到 Chunk 阶段测试 */

// 验证简单 IR 可降级为 chunk
TEST(IRLowerTest, SimpleLowerToChunk) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return 42;");
    auto ir_result = fixture.buildIR(unit);
    ASSERT_TRUE(ir_result.has_value());
    auto report = verifyModuleIRFlat(ir_result.value());
    ASSERT_TRUE(report.ok());

    auto lower_result = lowerModuleToChunk(ir_result.value());
    EXPECT_TRUE(lower_result.has_value()) << "Lower should succeed: " << lower_result.error();
}

// 验证复杂 IR 可降级
TEST(IRLowerTest, ComplexLowerToChunk) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return ((1+2)*(3+4))/(5%3);");
    auto ir_result = fixture.buildIR(unit);
    ASSERT_TRUE(ir_result.has_value());
    auto report = verifyModuleIRFlat(ir_result.value());
    ASSERT_TRUE(report.ok());

    auto lower_result = lowerModuleToChunk(ir_result.value());
    EXPECT_TRUE(lower_result.has_value());
    if (lower_result.has_value()) {
        EXPECT_FALSE(lower_result.value().functions.empty());
    }
}
