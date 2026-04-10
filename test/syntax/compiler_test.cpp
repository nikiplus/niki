#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/scanner.hpp"
#include "niki/vm/chunk.hpp"
#include "niki/vm/opcode.hpp"
#include <cstdint>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <sstream>

using namespace niki::syntax;
using namespace niki::vm;

// 基础的编译器测试夹具
class CompilerTest : public ::testing::Test {
  protected:
    ASTPool pool;

    // 辅助函数：直接将源码编译为 Chunk
    std::expected<niki::Chunk, CompileResultError> compileSource(std::string_view source) {
        Scanner scanner(source);
        std::vector<Token> tokens;

        bool scannerHadError = false;
        for (;;) {
            Token token = scanner.scanToken();
            tokens.push_back(token);
            if (token.type == TokenType::TOKEN_ERROR) {
                scannerHadError = true;
            }
            if (token.type == TokenType::TOKEN_EOF) {
                break;
            }
        }

        EXPECT_FALSE(scannerHadError) << "Scanner failed on input: " << source;

        Parser parser(source, tokens, pool);
        ASTNodeIndex root = parser.parse();
        EXPECT_FALSE(parser.hasError()) << "Parser failed on input: " << source;

        Compiler compiler;
        return compiler.compile(pool, root);
    }

    void SetUp() override { pool.clear(); }

    bool hasOpcode(const niki::Chunk &chunk, OPCODE opcode) {
        uint8_t op = static_cast<uint8_t>(opcode);
        for (uint8_t byte : chunk.code) {
            if (byte == op) {
                return true;
            }
        }
        return false;
    }
};

// 【分层测试】1. 最简单的加法表达式编译
TEST_F(CompilerTest, CompileSimpleAddition) {
    // 假设我们在全局作用域直接执行 1 + 2
    // 在目前的 NIKI 设计中，如果是全局表达式，可能会被包装在一个隐式的 ModuleDecl 中
    auto result = compileSource("var a = 1 + 2;");

    // 验证编译成功
    ASSERT_TRUE(result.has_value()) << "Compilation failed for simple addition.";

    const niki::Chunk &chunk = result.value();

    // 验证指令流是否包含关键操作码 (比如 OP_ADD, OP_LOAD_CONSTANT 等)
    // 这是一个非常基础的浅层验证，证明编译器生成了字节码而不是空的
    bool hasAdd = false;
    for (size_t i = 0; i < chunk.code.size(); ++i) {
        if (chunk.code[i] == static_cast<uint8_t>(OPCODE::OP_IADD) ||
            chunk.code[i] == static_cast<uint8_t>(OPCODE::OP_FADD)) {
            hasAdd = true;
            break;
        }
    }

    EXPECT_TRUE(hasAdd) << "Expected OP_IADD or OP_FADD instruction in chunk for '1 + 2'";
}

// 【整体测试】2. 编译整个 test.nk 文件
TEST_F(CompilerTest, CompileFullScript_TestNK) {
    std::string path1 = "scripts/test.nk";
    std::string path2 = "../scripts/test.nk";

    std::ifstream file(path1);
    if (!file.is_open()) {
        file.open(path2);
    }

    ASSERT_TRUE(file.is_open()) << "Failed to open test.nk";

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string sourceCode = buffer.str();

    auto result = compileSource(sourceCode);

    // 验证编译是否成功
    if (!result.has_value()) {
        const auto &errors = result.error().errors;
        for (const auto &err : errors) {
            std::cerr << "Compile Error at line " << err.line << ":" << err.column << " - " << err.message << std::endl;
        }
        FAIL() << "Compilation failed for test.nk. See console for error details.";
    }

    const niki::Chunk &chunk = result.value();

    // 简单验证：生成的字节码不为空，且常量池中有数据
    EXPECT_GT(chunk.code.size(), 0) << "Chunk code should not be empty for test.nk";
    EXPECT_GT(chunk.constants.size(), 0) << "Chunk constants should not be empty for test.nk";

    std::cout << "\n[CompilerTest] Successfully compiled test.nk into " << chunk.code.size()
              << " bytes of VM bytecode and " << chunk.constants.size() << " constants.\n"
              << std::endl;
}
TEST_F(CompilerTest, CompileArrayLiteral_UsesListElementsAsNodeIndices) {
    auto result = compileSource("var a = 10;\nvar b = 20;\nvar arr = [a, b, 30];\nreturn arr[1];");
    ASSERT_TRUE(result.has_value()) << "Array literal compilation should succeed.";

    const niki::Chunk &chunk = result.value();
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_NEW_ARRAY));
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_PUSH_ARRAY));
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_GET_ARRAY));
}

TEST_F(CompilerTest, CompileMapLiteral_UsesListElementsAsNodeIndices) {
    auto result = compileSource("var base = 1;\nvar m = {\"x\": base, \"y\": 2};\nreturn m[\"x\"];");
    ASSERT_TRUE(result.has_value()) << "Map literal compilation should succeed.";

    const niki::Chunk &chunk = result.value();
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_NEW_MAP));
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_SET_MAP));
};
TEST_F(CompilerTest, CompileAssignmentOperator_ModEqualReportsLocation) {
    auto result = compileSource("var a = 10;\na %= 3;");
    ASSERT_FALSE(result.has_value());

    const auto &errors = result.error().errors;
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].line, 2u);
    EXPECT_EQ(errors[0].column, 1u);
    EXPECT_EQ(errors[0].message, "Unsupported assignment operator.");
}

TEST_F(CompilerTest, CompileInvalidAssignmentTargetReportsLocation) {
    auto result = compileSource("(1 + 2) = 3;");
    ASSERT_FALSE(result.has_value());

    const auto &errors = result.error().errors;
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].line, 1u);
    EXPECT_EQ(errors[0].column, 1u);
    EXPECT_EQ(errors[0].message, "Invalid assignment target. Only simple variables are supported currently.");
}