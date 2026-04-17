#include "niki/semantic/type_checker.hpp"
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
    GlobalInterner interner;
    ASTPool pool{interner};

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

        niki::semantic::TypeChecker checker;
        auto checkResult = checker.check(pool, root);
        if (!checkResult.has_value()) {
            CompileResultError compile_errors;
            for (const auto &err : checkResult.error().errors) {
                compile_errors.errors.push_back(
                    CompileError{.line = err.line, .column = err.column, .message = err.message});
            }
            return std::unexpected(compile_errors);
        }

        Compiler compiler;
        auto type_table = pool.node_types;
        return compiler.compile(pool, root, type_table);
    }

    void SetUp() override { pool.clear(); }

    bool hasOpcode(const niki::Chunk &chunk, OPCODE opcode) {
        uint8_t op = static_cast<uint8_t>(opcode);
        for (const auto &funcVal : chunk.constants) {
            if (funcVal.type == niki::vm::ValueType::Object) {
                niki::vm::ObjFunction *objFunc = static_cast<niki::vm::ObjFunction *>(funcVal.as.object);
                for (uint8_t byte : objFunc->chunk.code) {
                    if (byte == op)
                        return true;
                }
            }
        }
        return false;
    }

    size_t countOpcode(const niki::Chunk &chunk, OPCODE opcode) {
        uint8_t op = static_cast<uint8_t>(opcode);
        size_t count = 0;
        for (const auto &funcVal : chunk.constants) {
            if (funcVal.type == niki::vm::ValueType::Object) {
                niki::vm::ObjFunction *objFunc = static_cast<niki::vm::ObjFunction *>(funcVal.as.object);
                for (uint8_t byte : objFunc->chunk.code) {
                    if (byte == op)
                        count++;
                }
            }
        }
        return count;
    }

    bool hasPatchedJump(const niki::Chunk &chunk, OPCODE opcode, size_t offsetHighIndex) {
        uint8_t op = static_cast<uint8_t>(opcode);
        for (const auto &funcVal : chunk.constants) {
            if (funcVal.type == niki::vm::ValueType::Object) {
                niki::vm::ObjFunction *objFunc = static_cast<niki::vm::ObjFunction *>(funcVal.as.object);
                for (size_t i = 0; i + offsetHighIndex + 1 < objFunc->chunk.code.size(); ++i) {
                    if (objFunc->chunk.code[i] == op) {
                        uint8_t high = objFunc->chunk.code[i + offsetHighIndex];
                        uint8_t low = objFunc->chunk.code[i + offsetHighIndex + 1];
                        if (!(high == 0xFF && low == 0xFF)) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }
};

// 【分层测试】1. 最简单的加法表达式编译
TEST_F(CompilerTest, CompileSimpleAddition) {
    // 假设我们在全局作用域直接执行 1 + 2
    // 在目前的 NIKI 设计中，如果是全局表达式，可能会被包装在一个隐式的 ModuleDecl 中
    auto result = compileSource("func test() { var a = 1 + 2; }");

    // 验证编译成功
    ASSERT_TRUE(result.has_value()) << "Compilation failed for simple addition.";

    const niki::Chunk &chunk = result.value();

    // 验证指令流是否包含关键操作码 (比如 OP_ADD, OP_LOAD_CONSTANT 等)
    // 注意：既然包在函数里了，代码是在函数的 chunk 里，而不是外层 chunk
    // 在 MVP 中，我们可以假设 globals 里只有一个叫 test 的函数
    bool hasAdd = false;
    for (const auto &func : chunk.constants) {
        if (func.type == niki::vm::ValueType::Object) {
            niki::vm::ObjFunction *objFunc = static_cast<niki::vm::ObjFunction *>(func.as.object);
            for (uint8_t byte : objFunc->chunk.code) {
                if (byte == static_cast<uint8_t>(OPCODE::OP_IADD) || byte == static_cast<uint8_t>(OPCODE::OP_FADD)) {
                    hasAdd = true;
                    break;
                }
            }
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
    auto result = compileSource("func test() { var a = 10;\nvar b = 20;\nvar arr = [a, b, 30];\nreturn arr[1]; }");
    ASSERT_TRUE(result.has_value()) << "Array literal compilation should succeed.";

    const niki::Chunk &chunk = result.value();
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_NEW_ARRAY));
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_PUSH_ARRAY));
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_GET_ARRAY));
}

TEST_F(CompilerTest, CompileMapLiteral_UsesListElementsAsNodeIndices) {
    auto result = compileSource("func test() { var base = 1;\nvar m = {\"x\": base, \"y\": 2};\nreturn m[\"x\"]; }");
    ASSERT_TRUE(result.has_value()) << "Map literal compilation should succeed.";

    const niki::Chunk &chunk = result.value();
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_NEW_MAP));
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_SET_MAP));
};
TEST_F(CompilerTest, CompileAssignmentOperator_ModEqualEmitsMod) {
    auto result = compileSource("func test() { var a = 10;\na %= 3; }");
    ASSERT_TRUE(result.has_value());

    const niki::Chunk &chunk = result.value();
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_IMOD));
}

TEST_F(CompilerTest, CompileInvalidAssignmentTargetReportsLocation) {
    auto result = compileSource("func test() { (1 + 2) = 3; }");
    ASSERT_FALSE(result.has_value());

    const auto &errors = result.error().errors;
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].line, 1u);
    EXPECT_EQ(errors[0].column, 15u);
    EXPECT_EQ(errors[0].message, "Invalid assignment target. Only simple variables are supported currently.");
}

TEST_F(CompilerTest, CompileIfElseAndLoop_JumpOffsetsArePatched) {
    auto result = compileSource("func test() { loop (true) {\n"
                                "if (false) {\n"
                                "break;\n"
                                "} else {\n"
                                "continue;\n"
                                "}\n"
                                "}\n"
                                "}\n");
    ASSERT_TRUE(result.has_value());
    const niki::Chunk &chunk = result.value();

    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_JZ));
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_JMP));
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_LOOP));

    EXPECT_TRUE(hasPatchedJump(chunk, OPCODE::OP_JZ, 2)) << "OP_JZ should have patched offset bytes.";
    EXPECT_TRUE(hasPatchedJump(chunk, OPCODE::OP_JMP, 1)) << "OP_JMP should have patched offset bytes.";
}

TEST_F(CompilerTest, CompileLoopBreakAndContinue_EmitExpectedControlOpcodes) {
    auto result = compileSource("func test() { loop (true) {\n"
                                "continue;\n"
                                "break;\n"
                                "}\n"
                                "}\n");
    ASSERT_TRUE(result.has_value());
    const niki::Chunk &chunk = result.value();

    EXPECT_GE(countOpcode(chunk, OPCODE::OP_LOOP), 2u);
    EXPECT_GE(countOpcode(chunk, OPCODE::OP_JMP), 1u);
    EXPECT_GE(countOpcode(chunk, OPCODE::OP_JZ), 1u);
}

TEST_F(CompilerTest, CompileVarDecl_DuplicateInSameScopeReportsError) {
    auto result = compileSource(R"(
func test() {
{
    var a = 1;
    var a = 2;
}
}
)");
    ASSERT_FALSE(result.has_value());
    const auto &errors = result.error().errors;
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].line, 5u);
    EXPECT_EQ(errors[0].column, 5u);
    EXPECT_EQ(errors[0].message, "Variable already declared in this scope.");
}

TEST_F(CompilerTest, CompileVarDecl_ShadowingInInnerScopeIsAllowed) {
    auto result = compileSource(R"(
func test() {
var a = 1;
{
    var a = 2;
}
var b = a;
return b;
}
)");
    ASSERT_TRUE(result.has_value());
}

TEST_F(CompilerTest, CompileVarDecl_OutOfScopeVariableReportsUndeclared) {
    auto result = compileSource(R"(
func test() {
{
    var x = 1;
}
var y = x;
}
)");
    ASSERT_FALSE(result.has_value());
    const auto &errors = result.error().errors;
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].line, 6u);
    EXPECT_EQ(errors[0].column, 9u);
    EXPECT_EQ(errors[0].message, "Undeclared variable.");
}

TEST_F(CompilerTest, CompileAssignmentOperator_BitAndEqualEmitsBitAnd) {
    auto result = compileSource("func test() { var a = 10;\na &= 3; }");
    ASSERT_TRUE(result.has_value());
    const niki::Chunk &chunk = result.value();
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_BIT_AND));
}

TEST_F(CompilerTest, CompileAssignmentOperator_BitOrEqualEmitsBitOr) {
    auto result = compileSource("func test() { var a = 10;\na |= 3; }");
    ASSERT_TRUE(result.has_value());
    const niki::Chunk &chunk = result.value();
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_BIT_OR));
}

TEST_F(CompilerTest, CompileAssignmentOperator_BitXorEqualEmitsBitXor) {
    auto result = compileSource("func test() { var a = 10;\na ^= 3; }");
    ASSERT_TRUE(result.has_value());
    const niki::Chunk &chunk = result.value();
    EXPECT_TRUE(hasOpcode(chunk, OPCODE::OP_BIT_XOR));
}

TEST_F(CompilerTest, SharedInterner_SameNameGetsSameIdAcrossPools) {
    ASTPool pool_a{interner};
    ASTPool pool_b{interner};

    uint32_t main_id_a = pool_a.internString("main");
    uint32_t main_id_b = pool_b.internString("main");
    uint32_t helper_id = pool_b.internString("helper");

    EXPECT_EQ(main_id_a, main_id_b);
    EXPECT_NE(main_id_a, helper_id);
}

TEST_F(CompilerTest, SharedInterner_ChunkStringPoolStableAcrossCompiles) {
    auto compile_with_pool = [](ASTPool &pool, std::string_view source) -> std::expected<niki::Chunk, CompileResultError> {
        Scanner scanner(source);
        std::vector<Token> tokens;

        bool scanner_had_error = false;
        for (;;) {
            Token token = scanner.scanToken();
            tokens.push_back(token);
            if (token.type == TokenType::TOKEN_ERROR) {
                scanner_had_error = true;
            }
            if (token.type == TokenType::TOKEN_EOF) {
                break;
            }
        }
        if (scanner_had_error) {
            return std::unexpected(CompileResultError{{CompileError{0, 0, "Scanner failed."}}});
        }

        Parser parser(source, tokens, pool);
        ASTNodeIndex root = parser.parse();
        if (parser.hasError()) {
            return std::unexpected(CompileResultError{{CompileError{0, 0, "Parser failed."}}});
        }

        niki::semantic::TypeChecker checker;
        auto check_result = checker.check(pool, root);
        if (!check_result.has_value()) {
            CompileResultError compile_errors;
            for (const auto &err : check_result.error().errors) {
                compile_errors.errors.push_back(CompileError{err.line, err.column, err.message});
            }
            return std::unexpected(compile_errors);
        }

        Compiler compiler;
        auto type_table = pool.node_types;
        return compiler.compile(pool, root, type_table);
    };

    ASTPool pool_a{interner};
    ASTPool pool_b{interner};

    auto chunk_a = compile_with_pool(pool_a, "func main() -> int { return 1; }");
    auto chunk_b = compile_with_pool(pool_b, "func helper() -> int { return 2; } func main() -> int { return helper(); }");

    ASSERT_TRUE(chunk_a.has_value());
    ASSERT_TRUE(chunk_b.has_value());

    auto main_id_opt = interner.find("main");
    ASSERT_TRUE(main_id_opt.has_value());
    uint32_t main_id = main_id_opt.value();

    ASSERT_LT(main_id, chunk_a->string_pool.size());
    ASSERT_LT(main_id, chunk_b->string_pool.size());
    EXPECT_EQ(chunk_a->string_pool[main_id], "main");
    EXPECT_EQ(chunk_b->string_pool[main_id], "main");
}
