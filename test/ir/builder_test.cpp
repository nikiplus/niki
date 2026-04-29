#include "niki/driver/driver.hpp"
#include "niki/l0_core/diagnostic/renderer.hpp"
#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/ir/verify.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace niki::ir::test {
namespace {

class IRBuilderTest : public ::testing::Test {
  protected:
    syntax::GlobalInterner interner;

    std::expected<ModuleIR, diagnostic::DiagnosticBag> buildModule(std::string_view source) {
        GlobalCompilationUnit unit(interner);
        unit.source_path = "<ir_builder_test>";
        unit.source = std::string(source);

        auto parse_result = driver::parseIntoCompilationUnit(unit);
        if (!parse_result.has_value()) {
            return std::unexpected(std::move(parse_result.error()));
        }

        IRBuilder builder;
        return builder.build(unit);
    }

    static size_t countInstKind(const ModuleIR &module_ir, InstKind instruction_kind) {
        size_t count = 0;
        for (const InstKind kind : module_ir.insts.kind) {
            if (kind == instruction_kind) {
                ++count;
            }
        }
        return count;
    }
};

TEST_F(IRBuilderTest, BuildIdentifierAssignAndCompoundAssign_ShouldEmitArithmeticAndMove) {
    auto result = buildModule(R"(
func test() {
    var a = 1;
    var b = 2;
    a = b;
    a += 3;
    a *= 4;
    return a;
}
)");
    ASSERT_TRUE(result.has_value())
        << "IR builder failed for identifier assignment sample.\n"
        << diagnostic::renderDiagnosticBagText(result.error());

    const ModuleIR &module_ir = result.value();
    EXPECT_GE(countInstKind(module_ir, InstKind::Move), 3u);
    EXPECT_GE(countInstKind(module_ir, InstKind::Add), 1u);
    EXPECT_GE(countInstKind(module_ir, InstKind::Mul), 1u);

    VerifyReport report = verifyModuleIRFlat(module_ir);
    EXPECT_TRUE(report.ok()) << "IR verify failed after builder output.";
}

TEST_F(IRBuilderTest, BuildArrayOrMemberAssign_ShouldReportUnsupportedLValueDiagnostic) {
    auto result = buildModule(R"(
func test() {
    var arr = [4, 5];
    arr[0] = 2;
}
)");
    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(result.error().empty());
    EXPECT_NE(diagnostic::renderDiagnosticBagText(result.error()).find("Only identifier assignment is supported"),
              std::string::npos);
}

TEST_F(IRBuilderTest, BuildStringLiteralConstant_StringIdMustBeInModulePoolRange) {
    auto result = buildModule(R"(
func test() {
    var greeting = "hello_ir";
    return greeting;
}
)");
    ASSERT_TRUE(result.has_value()) << diagnostic::renderDiagnosticBagText(result.error());

    const ModuleIR &module_ir = result.value();
    bool found_string_constant = false;
    for (size_t index = 0; index < module_ir.insts.size(); ++index) {
        if (module_ir.insts.kind[index] != InstKind::Constant || module_ir.insts.a_kind[index] != ValueKind::StringId) {
            continue;
        }
        found_string_constant = true;
        ASSERT_LT(static_cast<size_t>(module_ir.insts.a_u32[index]), module_ir.string_pool.size());
        EXPECT_EQ(module_ir.string_pool[module_ir.insts.a_u32[index]], "hello_ir");
    }
    EXPECT_TRUE(found_string_constant) << "Expected Constant(StringId) for string literal.";
}

TEST_F(IRBuilderTest, BuildIfElse_ShouldEmitBranchAndJumpAndRemainValid) {
    auto result = buildModule(R"(
func test(flag) {
    var x = 1;
    if (flag) {
        x = 10;
    } else {
        x = 20;
    }
    return x;
}
)");
    ASSERT_TRUE(result.has_value()) << diagnostic::renderDiagnosticBagText(result.error());
    const ModuleIR &module_ir = result.value();
    EXPECT_GE(countInstKind(module_ir, InstKind::Branch), 1u);
    EXPECT_GE(countInstKind(module_ir, InstKind::Jump), 2u);
}

TEST_F(IRBuilderTest, BuildCallExprWithUnresolvedCallee_ShouldReportDiagnostics) {
    auto result = buildModule(R"(
func test() {
    var a = 1;
    var b = 2;
    return callee(a + 1, b * 2);
}
)");
    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(result.error().empty());
    EXPECT_NE(diagnostic::renderDiagnosticBagText(result.error()).find("Identifier is unresolved"),
              std::string::npos);
}

TEST_F(IRBuilderTest, BreakContinueOutsideLoop_ShouldReportDiagnostics) {
    auto break_result = buildModule(R"(
func test() {
    break;
}
)");
    ASSERT_FALSE(break_result.has_value());
    EXPECT_FALSE(break_result.error().empty());

    auto continue_result = buildModule(R"(
func test() {
    continue;
}
)");
    ASSERT_FALSE(continue_result.has_value());
    EXPECT_FALSE(continue_result.error().empty());
}

} // namespace
} // namespace niki::ir::test

