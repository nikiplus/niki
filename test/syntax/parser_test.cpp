#include "ast_printer.hpp"
#include "niki/diagnostic/renderer.hpp"
#include "niki/syntax/ast.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/scanner.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

using namespace niki::syntax;
using namespace niki::syntax::test;

// 这是一个基础的测试夹具 (Test Fixture)
class ParserTest : public ::testing::Test {
  protected:
    GlobalInterner interner;
    ASTPool pool{interner};

    // 辅助函数：将源码字符串解析为 AST 根节点
    ASTNodeIndex parseSource(std::string_view source) {
        // 由于 scanner 没有一次性 scanTokens 方法，我们需要手动循环收集
        Scanner scanner(source);
        std::vector<Token> tokens;

        for (;;) {
            Token token = scanner.scanToken();
            tokens.push_back(token);
            if (token.type == TokenType::TOKEN_EOF) {
                break;
            }
        }
        auto scannerDiagnostics = scanner.takeDiagnostics();

        EXPECT_TRUE(scannerDiagnostics.empty())
            << "Scanner failed on input: " << source << "\n"
            << niki::diagnostic::renderDiagnosticBagText(scannerDiagnostics);

        Parser parser(source, tokens, pool);
        ParseResult result = parser.parse();
        EXPECT_TRUE(result.diagnostics.empty())
            << "Parser failed on input: " << source << "\n"
            << niki::diagnostic::renderDiagnosticBagText(result.diagnostics);
        return result.root;
    }

    void SetUp() override {
        // 每个测试用例运行前，清理 AST 池
        pool.clear();
    }
};

// 【测试用例 1】最简单的变量声明解析
TEST_F(ParserTest, ParseVarDecl_Simple) {
    auto root = parseSource("func test() { var x = 10; }");

    // 验证根节点是否生成成功
    ASSERT_TRUE(root.isvalid());

    ASTPrinter printer(pool);
    std::string astStr = printer.print(root);

    // 预期输出: (module\n  (func test() (block (var x = 10))))
    EXPECT_EQ(astStr, "(module\n  (func test() (block (var x = 10))))");
}

// 【测试用例 2】基础的二元表达式测试 (测试优先级和结合性)
TEST_F(ParserTest, ParseExpression_Precedence) {
    // 1 + 2 * 3 应该被解析为 1 + (2 * 3)
    auto root = parseSource("func test() { var result = 1 + 2 * 3; }");
    ASSERT_TRUE(root.isvalid());

    ASTPrinter printer(pool);
    std::string astStr = printer.print(root);

    // 预期输出: (module\n  (func test() (block (var result = (+ 1 (* 2 3))))))
    EXPECT_EQ(astStr, "(module\n  (func test() (block (var result = (+ 1 (* 2 3))))))");
}

// 【测试用例 3】解析完整的 test.nk 脚本文件
TEST_F(ParserTest, ParseFullScript_TestNK) {
    // 尝试两种路径以兼容不同的测试执行环境 (CTest 默认工作目录在 build，IDE 可能在项目根目录)
    std::string path1 = "scripts/test.nk";
    std::string path2 = "../scripts/test.nk";

    std::ifstream file(path1);
    if (!file.is_open()) {
        file.open(path2);
    }

    ASSERT_TRUE(file.is_open()) << "Failed to open test.nk from both " << path1 << " and " << path2;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string sourceCode = buffer.str();

    auto root = parseSource(sourceCode);
    ASSERT_TRUE(root.isvalid()) << "Root node is invalid for test.nk";

    ASTPrinter printer(pool);
    std::string astStr = printer.print(root);

    // 打印出来让用户能在控制台直观地看到整个 test.nk 的 AST 树
    std::cout << "\n========== AST Dump for test.nk ==========\n"
              << astStr << "\n==========================================\n"
              << std::endl;
}
