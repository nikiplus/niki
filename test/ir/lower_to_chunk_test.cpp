#include "../helpers/test_helpers.hpp"
#include "niki/l0_core/ir/lower_to_chunk.hpp"
#include "niki/l0_core/ir/verify.hpp"
#include "niki/l0_core/vm/opcode.hpp"
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

// Free IR 降级为 OP_FREE 字节码
TEST(IRLowerTest, FreeLowersToOpFree) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var s: string = \"x\"; return 0;");
    auto ir_result = fixture.buildIR(unit);
    ASSERT_TRUE(ir_result.has_value());
    auto lower_result = lowerModuleToChunk(ir_result.value());
    ASSERT_TRUE(lower_result.has_value()) << lower_result.error();

    const uint8_t op_free_byte = niki::vm::ToInt(niki::vm::OPCODE::OP_FREE);
    bool found = false;
    for (auto *fn : lower_result.value().functions) {
        if (fn == nullptr) {
            continue;
        }
        for (uint8_t b : fn->chunk.code) {
            if (b == op_free_byte) {
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }
    EXPECT_TRUE(found);
}
