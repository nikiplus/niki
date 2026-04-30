#include "niki/meta/precompile/precompile_pipeline.hpp"
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

        auto parse_result = meta::precompile::parseIntoCompilationUnit(unit);
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

    static bool hasExportedSymbol(const ModuleIR &module_ir, SymKind symbol_kind, std::string_view symbol_name) {
        for (const SymRecord &symbol_record : module_ir.syms) {
            if (!symbol_record.is_exported || symbol_record.sym_kind != symbol_kind ||
                symbol_record.sym_name_sid >= module_ir.string_pool.size()) {
                continue;
            }
            if (module_ir.string_pool[symbol_record.sym_name_sid] == symbol_name) {
                return true;
            }
        }
        return false;
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

TEST_F(IRBuilderTest, BuildKitsDecl_ShouldLowerToModuleKitsMetadata) {
    auto result = buildModule(R"(
component Position {}
component Velocity {}

kits MoveWindow {
    &Position as pos;
    Velocity as vel;
}

func main() {
    return 0;
}
)");
    ASSERT_TRUE(result.has_value()) << diagnostic::renderDiagnosticBagText(result.error());

    const ModuleIR &module_ir = result.value();
    ASSERT_EQ(module_ir.kits.size(), 1u);
    ASSERT_EQ(module_ir.kits_items.size(), 2u);

    const KitsRecord &kits = module_ir.kits[0];
    EXPECT_EQ(module_ir.string_pool[kits.kits_sid], "MoveWindow");
    ASSERT_EQ(kits.item_count, 2u);

    const KitsItemRecord &item0 = module_ir.kits_items[kits.first_item];
    const KitsItemRecord &item1 = module_ir.kits_items[kits.first_item + 1];
    EXPECT_EQ(module_ir.string_pool[item0.alias_sid], "pos");
    EXPECT_EQ(module_ir.string_pool[item0.component_sid], "Position");
    EXPECT_FALSE(item0.is_mutable);
    EXPECT_EQ(module_ir.string_pool[item1.alias_sid], "vel");
    EXPECT_EQ(module_ir.string_pool[item1.component_sid], "Velocity");
    EXPECT_TRUE(item1.is_mutable);
}

TEST_F(IRBuilderTest, BuildKitsDeclWithDuplicateAlias_ShouldReportIRDiagnostic) {
    auto result = buildModule(R"(
component Position {}
component Velocity {}

kits MoveWindow {
    &Position as pos;
    Velocity as pos;
}

func main() {
    return 0;
}
)");
    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(result.error().empty());
    EXPECT_NE(diagnostic::renderDiagnosticBagText(result.error()).find("Duplicate kits alias in IR lowering."),
              std::string::npos);
}

TEST_F(IRBuilderTest, BuildModuleDeclWithKits_ShouldUseSemanticModuleNameAsOwner) {
    auto result = buildModule(R"(
module gameplay {
component Position {}
kits MoveWindow {
    Position as pos;
}
func main() {
    return 0;
}
}
)");
    ASSERT_TRUE(result.has_value()) << diagnostic::renderDiagnosticBagText(result.error());
    const ModuleIR &module_ir = result.value();
    ASSERT_EQ(module_ir.kits.size(), 1u);
    const KitsRecord &kits = module_ir.kits[0];
    ASSERT_LT(static_cast<size_t>(kits.owner_mod_sid), module_ir.string_pool.size());
    EXPECT_EQ(module_ir.string_pool[kits.owner_mod_sid], "gameplay");
}

TEST_F(IRBuilderTest, BuildExportWrappedKits_ShouldMarkKitsExported) {
    auto result = buildModule(R"(
export kits MoveWindow {
    Position as pos;
}
component Position {}
func main() {
    return 0;
}
)");
    ASSERT_TRUE(result.has_value()) << diagnostic::renderDiagnosticBagText(result.error());
    const ModuleIR &module_ir = result.value();
    ASSERT_EQ(module_ir.kits.size(), 1u);
    EXPECT_TRUE(hasExportedSymbol(module_ir, SymKind::Kits, "MoveWindow"));
}

TEST_F(IRBuilderTest, BuildExportListKits_ShouldMarkKitsExportedRegardlessOrder) {
    auto before_result = buildModule(R"(
export {MoveWindow};
component Position {}
kits MoveWindow {
    Position as pos;
}
func main() {
    return 0;
}
)");
    ASSERT_TRUE(before_result.has_value()) << diagnostic::renderDiagnosticBagText(before_result.error());
    ASSERT_EQ(before_result->kits.size(), 1u);
    EXPECT_TRUE(hasExportedSymbol(before_result.value(), SymKind::Kits, "MoveWindow"));

    auto after_result = buildModule(R"(
component Position {}
kits MoveWindow {
    Position as pos;
}
export {MoveWindow};
func main() {
    return 0;
}
)");
    ASSERT_TRUE(after_result.has_value()) << diagnostic::renderDiagnosticBagText(after_result.error());
    ASSERT_EQ(after_result->kits.size(), 1u);
    EXPECT_TRUE(hasExportedSymbol(after_result.value(), SymKind::Kits, "MoveWindow"));
}

TEST_F(IRBuilderTest, BuildComponentDeclAndPromotion_ShouldLowerToComponentMetadata) {
    auto result = buildModule(R"(
struct vec { x: int, y: int }
component Position {}
component vec as vec_com;
func main() { return 0; }
)");
    ASSERT_TRUE(result.has_value()) << diagnostic::renderDiagnosticBagText(result.error());
    const ModuleIR &module_ir = result.value();
    ASSERT_EQ(module_ir.components.size(), 2u);

    const ComponentRecord &direct_component = module_ir.components[0];
    EXPECT_EQ(module_ir.string_pool[direct_component.component_sid], "Position");
    EXPECT_FALSE(direct_component.is_struct_promotion);

    const ComponentRecord &promoted_component = module_ir.components[1];
    EXPECT_EQ(module_ir.string_pool[promoted_component.component_sid], "vec_com");
    EXPECT_TRUE(promoted_component.is_struct_promotion);
    EXPECT_EQ(module_ir.string_pool[promoted_component.source_struct_sid], "vec");
}

TEST_F(IRBuilderTest, BuildExportWrappedAndListComponent_ShouldMarkComponentExported) {
    auto wrapped_result = buildModule(R"(
export component Position {}
func main() { return 0; }
)");
    ASSERT_TRUE(wrapped_result.has_value()) << diagnostic::renderDiagnosticBagText(wrapped_result.error());
    ASSERT_EQ(wrapped_result->components.size(), 1u);
    EXPECT_TRUE(hasExportedSymbol(wrapped_result.value(), SymKind::Component, "Position"));

    auto list_result = buildModule(R"(
component Position {}
export {Position};
func main() { return 0; }
)");
    ASSERT_TRUE(list_result.has_value()) << diagnostic::renderDiagnosticBagText(list_result.error());
    ASSERT_EQ(list_result->components.size(), 1u);
    EXPECT_TRUE(hasExportedSymbol(list_result.value(), SymKind::Component, "Position"));
}

} // namespace
} // namespace niki::ir::test

