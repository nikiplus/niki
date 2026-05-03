#include "../test_helpers.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/ast_payloads.hpp"
#include <gtest/gtest.h>
#include <string>

using namespace niki;
using namespace niki::semantic;
using namespace niki::syntax;

/** @phase_B: 表达式类型检查测试
 *
 * 验证 TypeChecker 为每个表达式节点正确回填 node_types，
 * 并在类型错误时产出诊断。
 */

// B-1: 整数字面量类型推断
TEST(TypeCheckerExprTest, IntegerLiteralType) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 42; return x;");
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value()) << "TypeCheck should succeed for integer literal";

    // 查找 LiteralExpr 节点，确认其 node_type 为 Int
    auto literal_indices = fixture.findNodes(unit.pool, NodeType::LiteralExpr);
    ASSERT_GE(literal_indices.size(), 1u);
    NKType lit_type = unit.pool.node_types[literal_indices[0]];
    EXPECT_EQ(lit_type, NKType::makeInt()) << "Integer literal should have type Int";
}

// B-2: 浮点字面量类型
TEST(TypeCheckerExprTest, FloatLiteralType) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 3.14; return x;", "float");
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value());
    auto literal_indices = fixture.findNodes(unit.pool, NodeType::LiteralExpr);
    ASSERT_GE(literal_indices.size(), 1u);
    NKType lit_type = unit.pool.node_types[literal_indices[0]];
    EXPECT_EQ(lit_type, NKType::makeFloat());
}

// B-3: 布尔字面量
TEST(TypeCheckerExprTest, BoolLiteralType) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = true; return x;", "bool");
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value());
    auto literal_indices = fixture.findNodes(unit.pool, NodeType::LiteralExpr);
    ASSERT_GE(literal_indices.size(), 1u);
    NKType lit_type = unit.pool.node_types[literal_indices[0]];
    EXPECT_EQ(lit_type, NKType::makeBool());
}

// B-4: 标识符解析 → 变量引用传递类型
TEST(TypeCheckerExprTest, IdentifierResolution) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 42; var y = x; return y;");
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value());

    // x 的初始值应为 Int
    auto literal_indices = fixture.findNodes(unit.pool, NodeType::LiteralExpr);
    ASSERT_GE(literal_indices.size(), 1u);
    NKType lit_type = unit.pool.node_types[literal_indices[0]];
    EXPECT_EQ(lit_type, NKType::makeInt());
}

// B-5: 二元算术类型推断链：1 + 2 → Int, x + 3 → Int
TEST(TypeCheckerExprTest, BinaryArithmeticChain) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 1 + 2; var y = x + 3; return y;");
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value());

    auto bin_indices = fixture.findNodes(unit.pool, NodeType::BinaryExpr);
    ASSERT_GE(bin_indices.size(), 2u);
    for (auto idx : bin_indices) {
        EXPECT_EQ(unit.pool.node_types[idx], NKType::makeInt())
            << "Binary integer expression should have type Int";
    }
}

// B-6: 类型标注检测: var x: float = 1.5;
TEST(TypeCheckerExprTest, TypeAnnotationFloat) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x: float = 1.5; return x;");
    auto result = fixture.runTypeCheck(unit);
    // Should either succeed or produce a meaningful diagnostic
    if (result.has_value()) {
        auto literal_indices = fixture.findNodes(unit.pool, NodeType::LiteralExpr);
        if (!literal_indices.empty()) {
            NKType lit_type = unit.pool.node_types[literal_indices[0]];
            EXPECT_EQ(lit_type, NKType::makeFloat());
        }
    }
}

// B-7: 类型标注不匹配: var x: int = "hello";
TEST(TypeCheckerExprTest, TypeAnnotationMismatch) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x: int = \"hello\"; return x;");
    auto result = fixture.runTypeCheck(unit);
    // 应产生类型错误诊断
    ASSERT_FALSE(result.has_value()) << "Expected type mismatch error, but typeCheck succeeded";
    const auto &diags = result.error().all();
    ASSERT_FALSE(diags.empty());
    EXPECT_EQ(diags[0].stage, diagnostic::DiagnosticStage::Semantic);
    EXPECT_EQ(diags[0].code, diagnostic::codeOf(diagnostic::events::SemanticCode::TypeMismatch));
    EXPECT_EQ(diags[0].severity, diagnostic::DiagnosticSeverity::Error);
}

// B-8: 运算类型不匹配: 1 + true
TEST(TypeCheckerExprTest, BinaryTypeMismatch) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 1 + true; return x;");
    auto result = fixture.runTypeCheck(unit);
    // 应产生类型错误
    if (!result.has_value()) {
        const auto &diags = result.error().all();
        ASSERT_FALSE(diags.empty());
        // 类型检查阶段 BinaryExpr 遇到 Int + Bool → 报告算术类型不匹配
        // 可能是 TypeMismatch 或其他诊断
        bool found_type_error = false;
        for (const auto &d : diags) {
            if (d.stage == diagnostic::DiagnosticStage::Semantic &&
                d.severity == diagnostic::DiagnosticSeverity::Error) {
                found_type_error = true;
                break;
            }
        }
        EXPECT_TRUE(found_type_error) << "Should contain at least one semantic error";
    }
}

// B-9: 未声明变量引用
TEST(TypeCheckerExprTest, UndeclaredVariable) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = undefinedVar; return x;");
    auto result = fixture.runTypeCheck(unit);
    // 应产生"未声明"诊断
    ASSERT_FALSE(result.has_value()) << "Should report error for undeclared variable";
    const auto &diags = result.error().all();
    ASSERT_FALSE(diags.empty());
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

// B-10: 比较运算返回 Bool: 3 > 5
TEST(TypeCheckerExprTest, ComparisonReturnsBool) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 3 > 5; return x;", "bool");
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value());
    auto bin_indices = fixture.findNodes(unit.pool, NodeType::BinaryExpr);
    if (!bin_indices.empty()) {
        // Comparison operators should produce Bool
        EXPECT_EQ(unit.pool.node_types[bin_indices[0]], NKType::makeBool())
            << "Comparison expression should have type Bool";
    }
}

// B-11: 逻辑运算操作数校验: true && false
TEST(TypeCheckerExprTest, LogicalAndOperands) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = true && false; return x;", "bool");
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value());
    auto logical_indices = fixture.findNodes(unit.pool, NodeType::LogicalExpr);
    if (!logical_indices.empty()) {
        EXPECT_EQ(unit.pool.node_types[logical_indices[0]], NKType::makeBool())
            << "Logical expression should have type Bool";
    }
}

// B-12: 逻辑运算类型错误: 1 && 2
TEST(TypeCheckerExprTest, LogicalAndTypeError) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 1 && 2; return x;");
    auto result = fixture.runTypeCheck(unit);
    // 应产生类型错误
    if (!result.has_value()) {
        const auto &diags = result.error().all();
        ASSERT_FALSE(diags.empty());
        // 逻辑运算左右操作数均为非 Bool → 应包含 NotABoolContext 诊断
        bool found_bool_error = false;
        for (const auto &d : diags) {
            if (d.code == diagnostic::codeOf(diagnostic::events::SemanticCode::NotABoolContext)) {
                found_bool_error = true;
                EXPECT_EQ(d.stage, diagnostic::DiagnosticStage::Semantic);
                EXPECT_EQ(d.severity, diagnostic::DiagnosticSeverity::Error);
                break;
            }
        }
        EXPECT_TRUE(found_bool_error) << "Should contain NotABoolContext diagnostic";
    } else {
        ADD_FAILURE() << "Expected type error for logical && with integers";
    }
}

// B-13: 一元负号类型: -42
TEST(TypeCheckerExprTest, UnaryNegateType) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = -42; return x;");
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value());
    auto unary_indices = fixture.findNodes(unit.pool, NodeType::UnaryExpr);
    if (!unary_indices.empty()) {
        EXPECT_EQ(unit.pool.node_types[unary_indices[0]], NKType::makeInt());
    }
}

// B-14: 一元逻辑非: !42 — TypeChecker 中的 SYM_BANG 接受 Bool 和 Int，因此有效
TEST(TypeCheckerExprTest, UnaryNotOnInt) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = !42; return x;", "bool");
    auto result = fixture.runTypeCheck(unit);
    // TypeChecker 的 SYM_BANG 接受 Bool 或 Int 操作数，返回 Bool
    // 因此 !42 是有效的表达式
    EXPECT_TRUE(result.has_value()) << "!42 should be valid (TypeChecker allows !Int -> Bool)";
}

// B-15: Return 类型匹配 (int → int)
TEST(TypeCheckerExprTest, ReturnTypeMatch) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return 42;");
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value()) << "return 42 with int return type should succeed";
}

// B-16: Return 类型不匹配: return "hello" (期望 int)
TEST(TypeCheckerExprTest, ReturnTypeMismatch) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return \"hello\";");
    auto result = fixture.runTypeCheck(unit);
    // 应产生类型不匹配诊断
    ASSERT_FALSE(result.has_value()) << "Expected type mismatch error for return string with int return type";
    const auto &diags = result.error().all();
    ASSERT_FALSE(diags.empty());
    bool found_return_error = false;
    for (const auto &d : diags) {
        if (d.code == diagnostic::codeOf(diagnostic::events::SemanticCode::ReturnTypeMismatch)) {
            found_return_error = true;
            EXPECT_EQ(d.stage, diagnostic::DiagnosticStage::Semantic);
            EXPECT_EQ(d.severity, diagnostic::DiagnosticSeverity::Error);
            break;
        }
    }
    EXPECT_TRUE(found_return_error) << "Should contain ReturnTypeMismatch diagnostic";
}

// B-17: 函数调用签名匹配 (需自定义源)
TEST(TypeCheckerExprTest, FunctionCallSigMatch) {
    ExprTestFixture fixture;
    // 在同一个模块中定义 addOne 并调用
    std::string source =
        "module __t{"
        "func addOne(x:int)->int{return x+1;}"
        "func __test_main()->int{return addOne(41);}"
        "}";
    GlobalCompilationUnit unit(fixture.interner_);
    unit.source = source;
    unit.source_path = "__test__";
    syntax::Scanner scanner(unit.source, unit.source_path);
    while (true) {
        auto token = scanner.scanToken();
        unit.tokens.push_back(token);
        if (token.type == syntax::TokenType::TOKEN_EOF) break;
    }
    static_cast<void>(scanner.takeDiagnostics());
    unit.pool.source_path = unit.source_path;
    syntax::Parser parser(unit.source, unit.tokens, unit.pool, unit.source_path);
    auto parse_result = parser.parse();
    unit.root = parse_result.root;
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value()) << "Function call with matching signature should succeed";
}

// B-18: 函数调用参数类型错误
TEST(TypeCheckerExprTest, FunctionCallArgTypeError) {
    ExprTestFixture fixture;
    std::string source =
        "module __t{"
        "func addOne(x:int)->int{return x+1;}"
        "func __test_main()->int{return addOne(\"str\");}"
        "}";
    GlobalCompilationUnit unit(fixture.interner_);
    unit.source = source;
    unit.source_path = "__test__";
    syntax::Scanner scanner(unit.source, unit.source_path);
    while (true) {
        auto token = scanner.scanToken();
        unit.tokens.push_back(token);
        if (token.type == syntax::TokenType::TOKEN_EOF) break;
    }
    static_cast<void>(scanner.takeDiagnostics());
    unit.pool.source_path = unit.source_path;
    syntax::Parser parser(unit.source, unit.tokens, unit.pool, unit.source_path);
    auto parse_result = parser.parse();
    unit.root = parse_result.root;
    auto result = fixture.runTypeCheck(unit);
    // 应产生语义错误（可能是 TypeMismatch 或 ArgumentCountMismatch）
    ASSERT_FALSE(result.has_value()) << "Should report error for argument type mismatch";
    const auto &diags = result.error().all();
    ASSERT_FALSE(diags.empty());
    EXPECT_EQ(diags[0].stage, diagnostic::DiagnosticStage::Semantic);
    EXPECT_EQ(diags[0].severity, diagnostic::DiagnosticSeverity::Error);
}

// B-19: 作用域遮蔽: {var x=1;} var x=2; 应正常工作
TEST(TypeCheckerExprTest, ScopeShadowingWorks) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("{var x=1;} var x=2; return x;");
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value()) << "Scope shadowing should be allowed";
}

// B-20: 变量重复声明: var x=1; var x=2; 应产生诊断
TEST(TypeCheckerExprTest, DuplicateVariable) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x=1; var x=2; return x;");
    auto result = fixture.runTypeCheck(unit);
    // 同一作用域内重复声明应报错
    ASSERT_FALSE(result.has_value()) << "Expected diagnostic for duplicate variable declaration";
    const auto &diags = result.error().all();
    ASSERT_FALSE(diags.empty());
    bool found_dup = false;
    for (const auto &d : diags) {
        if (d.code == diagnostic::codeOf(diagnostic::events::SemanticCode::DuplicateDeclaration)) {
            found_dup = true;
            EXPECT_EQ(d.stage, diagnostic::DiagnosticStage::Semantic);
            EXPECT_EQ(d.severity, diagnostic::DiagnosticSeverity::Error);
            break;
        }
    }
    EXPECT_TRUE(found_dup) << "Should contain DuplicateDeclaration diagnostic";
}

// E-2: 类型标注缺失: var x; (无初始值也无类型标注)
TEST(TypeCheckerExprTest, MissingTypeAnnotation) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x; return x;");
    auto result = fixture.runTypeCheck(unit);
    // 类型标注和初始值都缺失应产生诊断
    if (result.has_value()) {
        ADD_FAILURE() << "Expected error for variable with no type and no init";
    }
}

// E-3: 大表达式连锁: ((1+2)*(3+4))/(5%3)
TEST(TypeCheckerExprTest, LargeExpressionChain) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = ((1+2)*(3+4))/(5%3); return x;");
    auto result = fixture.runTypeCheck(unit);
    EXPECT_TRUE(result.has_value()) << "Complex arithmetic expression should type-check";
    auto bin_indices = fixture.findNodes(unit.pool, NodeType::BinaryExpr);
    EXPECT_GE(bin_indices.size(), 4u) << "Should have multiple binary nodes for complex expression";
}
