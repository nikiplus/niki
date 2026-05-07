#include "../helpers/test_helpers.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <gtest/gtest.h>

using namespace niki;
using namespace niki::vm;

/** @phase_R: 控制流端到端 — compileAndRun 从源码经全管线到 VM 返回值 */

// R-1: if-else then 分支
TEST(EndpointControlFlowTest, IfElseReturnsThenBranch) {
    ExprTestFixture fixture;
    auto val = fixture.compileAndRun("if (1 > 0) { return 100; } else { return 0; }");
    ASSERT_TRUE(val.has_value()) << "if/else compile+run should succeed";
    EXPECT_EQ(val.value().type, ValueType::Integer);
    EXPECT_EQ(val.value().as.integer, 100);
}

// R-2: if-else else 分支
TEST(EndpointControlFlowTest, IfElseReturnsElseBranch) {
    ExprTestFixture fixture;
    auto val = fixture.compileAndRun("if (1 < 0) { return 100; } else { return 42; }");
    ASSERT_TRUE(val.has_value()) << "if/else (else) compile+run should succeed";
    EXPECT_EQ(val.value().type, ValueType::Integer);
    EXPECT_EQ(val.value().as.integer, 42);
}

// R-3: if 无 else 分支
TEST(EndpointControlFlowTest, IfWithoutElse) {
    ExprTestFixture fixture;
    auto val = fixture.compileAndRun("if (1 > 0) { return 77; } return 0;");
    ASSERT_TRUE(val.has_value()) << "if (no else) compile+run should succeed";
    EXPECT_EQ(val.value().type, ValueType::Integer);
    EXPECT_EQ(val.value().as.integer, 77);
}

// R-4: loop + break
TEST(EndpointControlFlowTest, LoopBreakReturnsAfterLoop) {
    ExprTestFixture fixture;
    auto val = fixture.compileAndRun("loop { break; } return 42;");
    ASSERT_TRUE(val.has_value()) << "loop+break compile+run should succeed";
    EXPECT_EQ(val.value().type, ValueType::Integer);
    EXPECT_EQ(val.value().as.integer, 42);
}

// R-5: 条件 loop 计数
TEST(EndpointControlFlowTest, ConditionalLoopCounts) {
    ExprTestFixture fixture;
    auto val = fixture.compileAndRun("var x = 0; loop (x < 3) { x = x + 1; } return x;");
    ASSERT_TRUE(val.has_value()) << "conditional loop should compile+run";
    EXPECT_EQ(val.value().type, ValueType::Integer);
    EXPECT_EQ(val.value().as.integer, 3);
}

// R-6: 条件 loop 早期 break
TEST(EndpointControlFlowTest, ConditionalLoopEarlyBreak) {
    ExprTestFixture fixture;
    auto val = fixture.compileAndRun("var x = 0; loop (x < 100) { x = x + 1; if (x == 5) { break; } } return x;");
    ASSERT_TRUE(val.has_value()) << "conditional loop early break should compile+run";
    EXPECT_EQ(val.value().type, ValueType::Integer);
    EXPECT_EQ(val.value().as.integer, 5);
}

// R-7: 赋值语句执行
TEST(EndpointControlFlowTest, AssignmentExecute) {
    ExprTestFixture fixture;
    auto val = fixture.compileAndRun("var x = 10; x = 20; return x;");
    ASSERT_TRUE(val.has_value()) << "assignment compile+run should succeed";
    EXPECT_EQ(val.value().type, ValueType::Integer);
    EXPECT_EQ(val.value().as.integer, 20);
}

// R-8: 嵌套 if
TEST(EndpointControlFlowTest, NestedIf) {
    ExprTestFixture fixture;
    auto val = fixture.compileAndRun("var x = 0; if (1 > 0) { if (2 > 1) { x = 99; } } return x;");
    ASSERT_TRUE(val.has_value()) << "nested if compile+run should succeed";
    EXPECT_EQ(val.value().type, ValueType::Integer);
    EXPECT_EQ(val.value().as.integer, 99);
}
