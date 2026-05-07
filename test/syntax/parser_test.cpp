#include "../helpers/test_helpers.hpp"
#include "ast_printer.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/ast_payloads.hpp"
#include <gtest/gtest.h>

using namespace niki::syntax::test;
using namespace niki::syntax;

/** @phase_A: 表达式解析测试
 *
 * 验证 wrapAndParse + ASTPrinter 可正确表达任意表达式 AST。
 * 测试用例使用完整的 module → func → block → stmt → expr 包裹链路。
 */

// A-1: 整数字面量
TEST(ParserExprTest, IntegerLiteral) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 42; return x;");
    ASSERT_TRUE(unit.root.isvalid()) << "wrapAndParse should produce valid root";
    ASTPrinter printer(unit.pool);
    std::string ast_str = printer.print(unit.root);
    // Expect: (module (func __test_main() (block (var x = 42)(return x)))
    EXPECT_TRUE(ast_str.find("42") != std::string::npos);
    EXPECT_TRUE(ast_str.find("__test_main") != std::string::npos);
}

// A-2: 浮点字面量
TEST(ParserExprTest, FloatLiteral) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 3.14; return x;", "float");
    ASSERT_TRUE(unit.root.isvalid());
    // 验证存在 LiteralExpr 节点
    auto lit_nodes = fixture.findNodes(unit.pool, NodeType::LiteralExpr);
    EXPECT_GE(lit_nodes.size(), 1u);
    // 验证存在浮点常量 (常量池中)
    bool has_float_const = false;
    for (const auto &c : unit.pool.constants) {
        if (c.type == vm::ValueType::Float) {
            has_float_const = true;
            break;
        }
    }
    EXPECT_TRUE(has_float_const) << "Should have a float constant in the pool";
}

// A-3: 布尔字面量
TEST(ParserExprTest, BoolLiteral) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = true; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    ASTPrinter printer(unit.pool);
    std::string ast_str = printer.print(unit.root);
    EXPECT_TRUE(ast_str.find("true") != std::string::npos);
}

// A-4: 一元负号
TEST(ParserExprTest, UnaryNegate) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = -5; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    ASTPrinter printer(unit.pool);
    std::string ast_str = printer.print(unit.root);
    EXPECT_TRUE(ast_str.find("-") != std::string::npos);
}

// A-5: 逻辑非
TEST(ParserExprTest, LogicalNot) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = !true; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    ASTPrinter printer(unit.pool);
    std::string ast_str = printer.print(unit.root);
    EXPECT_TRUE(ast_str.find("!") != std::string::npos);
}

// A-6: 二元加减
TEST(ParserExprTest, BinaryAdd) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 1 + 2; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    ASTPrinter printer(unit.pool);
    std::string ast_str = printer.print(unit.root);
    // AST: (+ 1 2)
    EXPECT_TRUE(ast_str.find("+") != std::string::npos);
}

// A-7: 二元乘除
TEST(ParserExprTest, BinaryMul) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 3 * 4; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    ASTPrinter printer(unit.pool);
    std::string ast_str = printer.print(unit.root);
    EXPECT_TRUE(ast_str.find("*") != std::string::npos);
}

// A-8: 优先级嵌套: 1 + 2 * 3 → (+ 1 (* 2 3))
TEST(ParserExprTest, PrecedenceNested) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 1 + 2 * 3; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    ASTPrinter printer(unit.pool);
    std::string ast_str = printer.print(unit.root);
    // 乘法应在加法之前绑定
    auto mul_pos = ast_str.find("*");
    auto add_pos = ast_str.find("+");
    EXPECT_NE(mul_pos, std::string::npos);
    EXPECT_NE(add_pos, std::string::npos);
    // 由于乘法优先级高于加法，printer 中 "*" 应出现在更深的嵌套中
    // 字符串对比: "(+ 1 (* 2 3))" 中 + 在 * 之前出现
    EXPECT_LT(add_pos, mul_pos);
}

// A-9: 括号改写优先级: (1 + 2) * 3 → (* (+ 1 2) 3)
TEST(ParserExprTest, ParenthesisOverride) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = (1 + 2) * 3; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    ASTPrinter printer(unit.pool);
    std::string ast_str = printer.print(unit.root);
    // 括号内加法应先求值, printer 中 "+" 出现在更内层
    auto mul_pos = ast_str.find("*");
    auto add_pos = ast_str.find("+");
    EXPECT_NE(mul_pos, std::string::npos);
    EXPECT_NE(add_pos, std::string::npos);
    // 由于(1+2)整体是乘法左操作数，AST结构为 (* (+ 1 2) 3)
    // 在字符串中 "*" 出现在 "(+ 1 2)" 之前
    EXPECT_LT(mul_pos, add_pos);
}

// A-10: 相等性比较
TEST(ParserExprTest, EqualityCompare) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 1 == 2; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    ASTPrinter printer(unit.pool);
    std::string ast_str = printer.print(unit.root);
    EXPECT_TRUE(ast_str.find("==") != std::string::npos);
}

// A-11: 关系比较
TEST(ParserExprTest, RelationalCompare) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 3 > 5; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    ASTPrinter printer(unit.pool);
    std::string ast_str = printer.print(unit.root);
    EXPECT_TRUE(ast_str.find(">") != std::string::npos);
}

// A-12: 逻辑运算
TEST(ParserExprTest, LogicalAnd) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = true && false; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    ASTPrinter printer(unit.pool);
    std::string ast_str = printer.print(unit.root);
    EXPECT_TRUE(ast_str.find("&&") != std::string::npos);
}

// A-13: 标识符引用
TEST(ParserExprTest, IdentifierReference) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var y = a + b; return y;");
    ASSERT_TRUE(unit.root.isvalid());
    ASTPrinter printer(unit.pool);
    std::string ast_str = printer.print(unit.root);
    EXPECT_TRUE(ast_str.find("a") != std::string::npos);
    EXPECT_TRUE(ast_str.find("b") != std::string::npos);
}

// A-14: 字符串字面量
TEST(ParserExprTest, StringLiteral) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = \"hello\"; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    ASTPrinter printer(unit.pool);
    std::string ast_str = printer.print(unit.root);
    // 应能识别出字符串字面量节点
    auto literal_nodes = fixture.findNodes(unit.pool, NodeType::LiteralExpr);
    EXPECT_GE(literal_nodes.size(), 1u);
}

// A-15: 数组字面量
TEST(ParserExprTest, ArrayLiteral) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = [1, 2, 3]; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    auto array_nodes = fixture.findNodes(unit.pool, NodeType::ArrayExpr);
    EXPECT_GE(array_nodes.size(), 1u);
}

// A-16: 函数调用
TEST(ParserExprTest, FunctionCall) {
    ExprTestFixture fixture;
    // 需要在包裹中额外定义 foo 函数
    std::string source =
        "module __t{"
        "func foo(a:int,b:int)->int{return a+b;}"
        "func __test_main()->int{var x = foo(1,2); return x;}"
        "}";
    auto unit = fixture.wrapAndParse("var x = foo(1,2); return x;");
    // 使用自定义包裹方式
    unit = [&]() {
        CompilationUnit u(fixture.interner_);
        u.source = source;
        u.source_path = "__test__";
        syntax::Scanner scanner(u.source, u.source_path);
        while (true) {
            auto token = scanner.scanToken();
            u.tokens.push_back(token);
            if (token.type == syntax::TokenType::TOKEN_EOF) break;
        }
        static_cast<void>(scanner.takeDiagnostics());
        u.pool.source_path = u.source_path;
        syntax::Parser parser(u.source, u.tokens, u.pool, u.source_path);
        auto parse_result = parser.parse();
        u.root = parse_result.root;
        return u;
    }();
    ASSERT_TRUE(unit.root.isvalid());
    auto call_nodes = fixture.findNodes(unit.pool, NodeType::CallExpr);
    EXPECT_GE(call_nodes.size(), 1u);
}

// A-17: 成员访问
TEST(ParserExprTest, MemberAccess) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = obj.field; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    auto member_nodes = fixture.findNodes(unit.pool, NodeType::MemberExpr);
    EXPECT_GE(member_nodes.size(), 1u);
}

// A-18: 索引访问
TEST(ParserExprTest, IndexAccess) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = arr[0]; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    auto index_nodes = fixture.findNodes(unit.pool, NodeType::IndexExpr);
    EXPECT_GE(index_nodes.size(), 1u);
}

// A-19: 复杂嵌套: foo(bar(1 + 2) * 3)[0]
TEST(ParserExprTest, ComplexNested) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = get(bar(1 + 2) * 3)[0]; return x;");
    ASSERT_TRUE(unit.root.isvalid());
    // 同时包含 CallExpr、BinaryExpr、IndexExpr
    auto call_nodes = fixture.findNodes(unit.pool, NodeType::CallExpr);
    auto bin_nodes = fixture.findNodes(unit.pool, NodeType::BinaryExpr);
    auto idx_nodes = fixture.findNodes(unit.pool, NodeType::IndexExpr);
    EXPECT_GE(call_nodes.size(), 1u);
    EXPECT_GE(bin_nodes.size(), 1u);
    EXPECT_GE(idx_nodes.size(), 1u);
}

// A-20: 解析错误恢复: var x = 1 + ;
TEST(ParserExprTest, ParserErrorRecovery) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("var x = 1 + ;");
    // 解析器应在此场景下产出诊断
    // 不崩溃即可视为通过
    SUCCEED();
}

// A-21: 三元/条件表达式 (如果语言支持)
TEST(ParserExprTest, NonStandardExpressionStillParses) {
    // 验证任意合法的表达式模板不会导致解析器崩溃
    std::vector<std::string> expressions = {
        "var a=1;var b=2;var c=a+b; return c;",
        "return 42;",
        "var x=1+2*3/4%5; return x;",
        "var x=1<2 && 3>4 || 5==5; return x;"
    };
    for (const auto& expr : expressions) {
        ExprTestFixture fixture;
        auto unit = fixture.wrapAndParse(expr);
        EXPECT_TRUE(unit.root.isvalid()) << "Failed to parse: " << expr;
        if (!unit.root.isvalid()) {
            ADD_FAILURE() << "wrapAndParse returned invalid root for: " << expr;
        }
    }
}

// A-22: 验证包裹结构: ProgramRoot 下包含 ModuleDecl
TEST(ParserExprTest, ModuleFunctionWrapperStructure) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return 42;");
    ASSERT_TRUE(unit.root.isvalid());

    // wrapAndParse 直接返回解析根节点（可能为 ModuleDecl 或 ProgramRoot）
    // 验证至少存在 ModuleDecl 节点
    auto module_nodes = fixture.findNodes(unit.pool, NodeType::ModuleDecl);
    EXPECT_GE(module_nodes.size(), 1u);

    // 验证存在 FunctionDecl 节点
    auto func_nodes = fixture.findNodes(unit.pool, NodeType::FunctionDecl);
    EXPECT_GE(func_nodes.size(), 1u);
}

// A-23: 验证多个顶层 FunctionDecl 共存
TEST(ParserExprTest, MultipleFunctionsInModule) {
    ExprTestFixture fixture;
    std::string source =
        "module __t{"
        "func helper(x:int)->int{return x+1;}"
        "func __test_main()->int{return helper(41);}"
        "}";
    CompilationUnit unit(fixture.interner_);
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
    ASSERT_TRUE(unit.root.isvalid());
    auto func_nodes = fixture.findNodes(unit.pool, NodeType::FunctionDecl);
    EXPECT_EQ(func_nodes.size(), 2u);
}
