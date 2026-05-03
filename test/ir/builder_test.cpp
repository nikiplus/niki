#include "../test_helpers.hpp"
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/ir/verify.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <gtest/gtest.h>

using namespace niki;
using namespace niki::ir;
using namespace niki::vm;

/** @phase_C: IR 构建测试
 *
 * 验证 IRBuilder 为表达式正确发射指令序列。
 * 核心策略: compileAndRun 验证执行结果的正确性。
 */

// C-1: 整数常量 → Constant + Return
TEST(IRBuilderExprTest, IntegerConstant) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return 42;");
    auto ir_result = fixture.buildIR(unit);
    ASSERT_TRUE(ir_result.has_value()) << "IR build should succeed";

    const auto &ir = ir_result.value();
    ASSERT_GE(ir.funcs.size(), 1u);
    ASSERT_GE(ir.insts.size(), 2u); // Constant + Return

    // 验证指令序列: Constant, Return
    EXPECT_EQ(ir.insts.kind[0], InstKind::Constant);
    EXPECT_EQ(ir.insts.kind[ir.insts.size() - 1], InstKind::Return);

    // 验证 verify 通过
    auto report = verifyModuleIRFlat(ir);
    EXPECT_TRUE(report.ok()) << "IR should pass verification";
}

// C-2: 浮点常量
TEST(IRBuilderExprTest, FloatConstant) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return 3.14;", "float");
    auto ir_result = fixture.buildIR(unit);
    ASSERT_TRUE(ir_result.has_value());
    EXPECT_GE(ir_result.value().insts.size(), 2u);
}

// C-3: 布尔常量
TEST(IRBuilderExprTest, BoolConstant) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return true;", "bool");
    auto ir_result = fixture.buildIR(unit);
    ASSERT_TRUE(ir_result.has_value());
    EXPECT_GE(ir_result.value().insts.size(), 2u);

    auto report = verifyModuleIRFlat(ir_result.value());
    EXPECT_TRUE(report.ok());
}

// C-4: 二元加法: 1 + 2
TEST(IRBuilderExprTest, BinaryAdd) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return 1 + 2;");
    auto ir_result = fixture.buildIR(unit);
    ASSERT_TRUE(ir_result.has_value());
    EXPECT_GE(ir_result.value().insts.size(), 3u); // 2x Constant + Add

    // 验证有 Add 指令
    bool has_add = false;
    for (auto k : ir_result.value().insts.kind) {
        if (k == InstKind::Add)
            has_add = true;
    }
    EXPECT_TRUE(has_add) << "Should emit Add instruction for +";

    auto report = verifyModuleIRFlat(ir_result.value());
    EXPECT_TRUE(report.ok());
}

// C-5: 二元乘法: 3 * 4
TEST(IRBuilderExprTest, BinaryMul) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return 3 * 4;");
    auto ir_result = fixture.buildIR(unit);
    ASSERT_TRUE(ir_result.has_value());

    bool has_mul = false;
    for (auto k : ir_result.value().insts.kind) {
        if (k == InstKind::Mul)
            has_mul = true;
    }
    EXPECT_TRUE(has_mul);
}

// C-6: 优先级嵌套: 1 + 2 * 3
TEST(IRBuilderExprTest, PrecedenceNested) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return 1 + 2 * 3;");
    auto ir_result = fixture.buildIR(unit);
    ASSERT_TRUE(ir_result.has_value());

    bool has_add = false, has_mul = false;
    for (auto k : ir_result.value().insts.kind) {
        if (k == InstKind::Add)
            has_add = true;
        if (k == InstKind::Mul)
            has_mul = true;
    }
    EXPECT_TRUE(has_add);
    EXPECT_TRUE(has_mul);

    auto report = verifyModuleIRFlat(ir_result.value());
    EXPECT_TRUE(report.ok());
}

// C-7: 变量引用: var a = 10; return a;
TEST(IRBuilderExprTest, VariableReference) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var a = 10; return a;");
    auto ir_result = fixture.buildIR(unit);
    ASSERT_TRUE(ir_result.has_value());

    // VarDecl + Return should produce at least Constant + Move + Return
    EXPECT_GE(ir_result.value().insts.size(), 2u);

    auto report = verifyModuleIRFlat(ir_result.value());
    EXPECT_TRUE(report.ok());
}

// C-8: 一元负号: return -5;
TEST(IRBuilderExprTest, UnaryNegate) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return -5;");
    auto ir_result = fixture.buildIR(unit);
    ASSERT_TRUE(ir_result.has_value());

    bool has_neg = false;
    for (auto k : ir_result.value().insts.kind) {
        if (k == InstKind::Neg)
            has_neg = true;
    }
    EXPECT_TRUE(has_neg);
}

// C-9: 一元逻辑非: return !true;
TEST(IRBuilderExprTest, UnaryLogicNot) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return !true;", "bool");
    auto ir_result = fixture.buildIR(unit);
    ASSERT_TRUE(ir_result.has_value());

    bool has_logic_not = false;
    for (auto k : ir_result.value().insts.kind) {
        if (k == InstKind::LogicNot)
            has_logic_not = true;
    }
    EXPECT_TRUE(has_logic_not);
}

// C-10: 比较运算: return 3 > 5;
TEST(IRBuilderExprTest, CompareGreater) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return 3 > 5;", "bool");
    auto ir_result = fixture.buildIR(unit);
    ASSERT_TRUE(ir_result.has_value());

    bool has_cmp = false;
    for (auto k : ir_result.value().insts.kind) {
        if (k == InstKind::CmpGt)
            has_cmp = true;
    }
    EXPECT_TRUE(has_cmp);
}

// C-11: 复杂表达式: ((1+2)*(3+4))/(5%3) — 完整编译执行验证
TEST(IRBuilderExprTest, ComplexExpressionChain) {
    ExprTestFixture fixture;
    auto val_result = fixture.compileAndRun("return ((1+2)*(3+4))/(5%3);");
    ASSERT_TRUE(val_result.has_value());
    EXPECT_EQ(val_result.value().type, ValueType::Integer);
    // (3*7)/2 = 21/2 = 10 (integer division)
    EXPECT_EQ(val_result.value().as.integer, 10);
}

// C-12: 完整编译执行: return var
TEST(IRBuilderExprTest, VariableAddAndReturn) {
    ExprTestFixture fixture;
    auto val_result = fixture.compileAndRun("var x = 19; var y = 23; return x + y;");
    ASSERT_TRUE(val_result.has_value());
    EXPECT_EQ(val_result.value().type, ValueType::Integer);
    EXPECT_EQ(val_result.value().as.integer, 42);
}
