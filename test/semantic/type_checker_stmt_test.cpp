#include "../test_helpers.hpp"
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

// T-1: If 条件解析通过（当前 TypeChecker 不做条件类型校验，仅确保不崩溃）
TEST(TypeCheckerStmtTest, IfStatementParses) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse(
        "if (true) { return 1; } else { return 2; }"
    );
    auto result = fixture.runTypeCheck(unit);
    // 当前 checkIfStmt 只检查表达式但不强制 Bool 类型校验
    // 只要不崩溃即可
    SUCCEED();
}

// T-2: 赋值类型不匹配报错
TEST(TypeCheckerStmtTest, AssignmentTypeMismatch) {
    ExprTestFixture fixture;
    // 赋值 整数给布尔变量? 实际上前一句 var x: bool 声明了x为布尔
    // 但当前包装器默认返回 int，var x: bool = true 不能直接赋值 int
    // 改用最直接的触发路径：var x = 42; x = true; → 赋值类型不匹配
    // 但当前 TypeChecker 在赋值时检查左值和右值类型是否相等
    // var x = 42 使 x 为 int，然后 x = true 让左值 int 右值 bool
    auto unit = fixture.wrapAndParse(
        "var x = 42; x = true; return x;"
    );
    auto result = fixture.runTypeCheck(unit);
    // 赋值类型不匹配应产生 TypeMismatch 诊断
    if (result.has_value()) {
        ADD_FAILURE() << "Expected TypeMismatch error for assignment int = bool";
    } else {
        const auto &diags = result.error().all();
        ASSERT_FALSE(diags.empty());
        EXPECT_EQ(diags[0].stage, diagnostic::DiagnosticStage::Semantic);
        EXPECT_EQ(diags[0].code, diagnostic::codeOf(diagnostic::events::SemanticCode::TypeMismatch));
        EXPECT_EQ(diags[0].severity, diagnostic::DiagnosticSeverity::Error);
    }
}

// T-3: 缺少类型标注报错
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

// T-4: 未声明变量引用报错
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

// T-5: Return 类型匹配通过
TEST(TypeCheckerStmtTest, ReturnTypeMatch) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return 42;");
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value()) << "return 42 with int return type should succeed";
}

// T-6: Return 类型不匹配报错（返回字符串但函数声明返回 int）
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
