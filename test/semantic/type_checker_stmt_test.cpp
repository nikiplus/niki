#include "../helpers/test_helpers.hpp"
#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include <gtest/gtest.h>

using namespace niki;
using namespace niki::semantic;
using namespace niki::syntax;

/** @phase_T: 语句类型检查测试
 *
 * 验证 TypeChecker 为各语句节点正确执行类型校验并产出诊断。
 * 所有预期失败的测试断言 stage + code + severity 三要素。
 */

// T-1: If 条件为 Bool 通过
TEST(TypeCheckerStmtTest, IfStatementTypecheckPasses) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("if (true) { return 1; } else { return 2; }");
    auto result = fixture.runTypeCheck(unit);
    ASSERT_TRUE(result.has_value()) << "if (true) { ... } should typecheck successfully";
}

// T-2: If 条件非 Bool 报错（NotABoolContext）
TEST(TypeCheckerStmtTest, IfConditionNotBool) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("if (42) { return 1; }");
    auto result = fixture.runTypeCheck(unit);
    ASSERT_FALSE(result.has_value()) << "if (42) should fail typecheck";
    const auto &diags = result.error().all();
    ASSERT_FALSE(diags.empty());
    EXPECT_EQ(diags[0].stage, diagnostic::DiagnosticStage::Semantic);
    EXPECT_EQ(diags[0].code, diagnostic::codeOf(diagnostic::events::SemanticCode::NotABoolContext));
    EXPECT_EQ(diags[0].severity, diagnostic::DiagnosticSeverity::Error);
}

// T-3: Loop 条件为 Bool 通过
TEST(TypeCheckerStmtTest, LoopConditionBoolPasses) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 0; loop (x < 10) { x = x + 1; } return x;");
    auto result = fixture.runTypeCheck(unit);
    ASSERT_TRUE(result.has_value()) << "loop (cond_bool) { ... } should typecheck";
}

// T-4: Loop 条件非 Bool 报错
TEST(TypeCheckerStmtTest, LoopConditionNotBool) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 0; loop (42) { x = x + 1; } return x;");
    auto result = fixture.runTypeCheck(unit);
    ASSERT_FALSE(result.has_value()) << "loop (int) should fail typecheck";
    const auto &diags = result.error().all();
    ASSERT_FALSE(diags.empty());
    bool has_not_bool = false;
    for (const auto &d : diags) {
        if (d.code == diagnostic::codeOf(diagnostic::events::SemanticCode::NotABoolContext)) {
            has_not_bool = true;
            break;
        }
    }
    EXPECT_TRUE(has_not_bool) << "Should contain NotABoolContext diagnostic";
}

// T-5: 赋值类型匹配通过
TEST(TypeCheckerStmtTest, AssignmentTypeMatch) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 42; x = 100; return x;");
    auto result = fixture.runTypeCheck(unit);
    ASSERT_TRUE(result.has_value()) << "int-to-int assignment should typecheck";
}

// T-6: 赋值类型不匹配报错
TEST(TypeCheckerStmtTest, AssignmentTypeMismatch) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 42; x = true; return x;");
    auto result = fixture.runTypeCheck(unit);
    ASSERT_FALSE(result.has_value()) << "int = bool should fail typecheck";
    const auto &diags = result.error().all();
    ASSERT_FALSE(diags.empty());
    EXPECT_EQ(diags[0].stage, diagnostic::DiagnosticStage::Semantic);
    EXPECT_EQ(diags[0].code, diagnostic::codeOf(diagnostic::events::SemanticCode::TypeMismatch));
    EXPECT_EQ(diags[0].severity, diagnostic::DiagnosticSeverity::Error);
}

// T-7: 缺少类型标注报错
TEST(TypeCheckerStmtTest, MissingTypeAnnotation) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x; return 42;");
    auto result = fixture.runTypeCheck(unit);
    // var x 既没有类型标注也没有初始值，应产生 MissingTypeAnnotation
    if (result.has_value()) {
        ADD_FAILURE() << "Expected MissingTypeAnnotation error for var x with no type/init";
    } else {
        const auto &diags = result.error().all();
        ASSERT_FALSE(diags.empty());
        EXPECT_EQ(diags[0].stage, diagnostic::DiagnosticStage::Semantic);
        EXPECT_EQ(diags[0].code, diagnostic::codeOf(diagnostic::events::SemanticCode::MissingTypeAnnotation));
        EXPECT_EQ(diags[0].severity, diagnostic::DiagnosticSeverity::Error);
    }
}

// T-8: 未声明变量引用报错
TEST(TypeCheckerStmtTest, UndeclaredIdentifier) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = unknownVar; return x;");
    auto result = fixture.runTypeCheck(unit);
    if (result.has_value()) {
        ADD_FAILURE() << "Expected UndeclaredIdentifier error for unknownVar";
    } else {
        const auto &diags = result.error().all();
        ASSERT_FALSE(diags.empty());
        // 第一个诊断可能是变量初值的类型错误（未知变量导致 init 类型推断失败）
        // 也可能是后续的 MissingTypeAnnotation
        // 验证至少有一条是 UndeclaredIdentifier
        bool found_undeclared = false;
        for (const auto &d : diags) {
            if (d.code == diagnostic::codeOf(diagnostic::events::SemanticCode::UndeclaredIdentifier)) {
                found_undeclared = true;
                EXPECT_EQ(d.stage, diagnostic::DiagnosticStage::Semantic);
                EXPECT_EQ(d.severity, diagnostic::DiagnosticSeverity::Error);
                break;
            }
        }
        EXPECT_TRUE(found_undeclared) << "Should contain UndeclaredIdentifier diagnostic";
    }
}

// T-9: Return 类型匹配通过
TEST(TypeCheckerStmtTest, ReturnTypeMatch) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return 42;");
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value()) << "return 42 with int return type should succeed";
}

// T-10: Return 类型不匹配报错（返回字符串但函数声明返回 int）
TEST(TypeCheckerStmtTest, ReturnTypeMismatch) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return \"hello\";");
    auto result = fixture.runTypeCheck(unit);
    if (result.has_value()) {
        ADD_FAILURE() << "Expected ReturnTypeMismatch error for return string with int return type";
    } else {
        const auto &diags = result.error().all();
        ASSERT_FALSE(diags.empty());
        EXPECT_EQ(diags[0].stage, diagnostic::DiagnosticStage::Semantic);
        EXPECT_EQ(diags[0].code, diagnostic::codeOf(diagnostic::events::SemanticCode::ReturnTypeMismatch));
        EXPECT_EQ(diags[0].severity, diagnostic::DiagnosticSeverity::Error);
    }
}
