#include "niki/driver/driver.hpp"
#include "niki/l0_core/diagnostic/renderer.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace {
namespace fs = std::filesystem;

std::string resolveCaseDirOrDie(const std::string &relative_case_path) {
    const std::string path_a = "scripts/" + relative_case_path;
    const std::string path_b = "../scripts/" + relative_case_path;
    if (fs::exists(path_a)) {
        return path_a;
    }
    if (fs::exists(path_b)) {
        return path_b;
    }
    ADD_FAILURE() << "Failed to resolve case dir from " << path_a << " and " << path_b;
    return path_a;
}
} // namespace

TEST(DriverProjectTest, MultiFileBasicCaseRunsSuccessfully) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/success/01_multi_file_basic"), options);
    ASSERT_TRUE(result.has_value()) << niki::diagnostic::renderDiagnosticBagText(result.error());
    ASSERT_EQ(result->type, niki::vm::ValueType::Integer);
    EXPECT_EQ(result->as.integer, 42);
}

TEST(DriverProjectTest, MultiFileInitOrderCaseRunsSuccessfully) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/success/02_init_order"), options);
    ASSERT_TRUE(result.has_value()) << niki::diagnostic::renderDiagnosticBagText(result.error());
    ASSERT_EQ(result->type, niki::vm::ValueType::Integer);
    EXPECT_EQ(result->as.integer, 77);
}

TEST(DriverProjectTest, MultiDeclStableCaseRunsSuccessfully) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/success/03_multi_decl_stable"), options);
    ASSERT_TRUE(result.has_value()) << niki::diagnostic::renderDiagnosticBagText(result.error());
    ASSERT_EQ(result->type, niki::vm::ValueType::Integer);
    EXPECT_EQ(result->as.integer, 100);
}

TEST(DriverProjectTest, DiceBasicCaseReportsUnsupportedOperatorInCurrentIRBuilder) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/success/04_dice_basic"), options);
    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(result.error().empty());
    const auto &diagnostics = result.error().all();
    EXPECT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics[0].stage, niki::diagnostic::DiagnosticStage::IR);
    EXPECT_EQ(diagnostics[0].severity, niki::diagnostic::DiagnosticSeverity::Error);
    EXPECT_NE(niki::diagnostic::renderDiagnosticBagText(result.error()).find("Unsupported binary operator"),
              std::string::npos);
}

TEST(DriverProjectTest, ExplicitImportCaseRunsSuccessfully) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/success/05_import_explicit"), options);
    ASSERT_TRUE(result.has_value()) << niki::diagnostic::renderDiagnosticBagText(result.error());
    ASSERT_EQ(result->type, niki::vm::ValueType::Integer);
    EXPECT_EQ(result->as.integer, 42);
}

TEST(DriverProjectTest, MissingImportedSymbolShouldFailBeforeRuntime) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/fail/semantic_02_import_missing_symbol"), options);
    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(result.error().empty());
    EXPECT_NE(niki::diagnostic::renderDiagnosticBagText(result.error()).find("Imported symbol not exported."),
              std::string::npos);
}

TEST(DriverProjectTest, ModuleScopedImportMissingSymbolShouldFailBeforeRuntime) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result =
        driver.runProject(resolveCaseDirOrDie("cases/fail/semantic_module_scoped_import_missing_symbol"), options);
    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(result.error().empty());
    EXPECT_NE(niki::diagnostic::renderDiagnosticBagText(result.error()).find("Imported symbol not exported."),
              std::string::npos);
}

TEST(DriverProjectTest, TypeAliasBasicCaseRunsSuccessfully) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/success/07_typealias_basic"), options);
    ASSERT_TRUE(result.has_value()) << niki::diagnostic::renderDiagnosticBagText(result.error());
    ASSERT_EQ(result->type, niki::vm::ValueType::Integer);
    EXPECT_EQ(result->as.integer, 41);
}

TEST(DriverProjectTest, FunctionArityCaseRunsSuccessfully) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/success/08_function_arity"), options);
    ASSERT_TRUE(result.has_value()) << niki::diagnostic::renderDiagnosticBagText(result.error());
    ASSERT_EQ(result->type, niki::vm::ValueType::Integer);
    EXPECT_EQ(result->as.integer, 42);
}

TEST(DriverProjectTest, ComponentPromotionCaseRunsSuccessfully) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/success/09_component_promotion"), options);
    ASSERT_TRUE(result.has_value()) << niki::diagnostic::renderDiagnosticBagText(result.error());
    ASSERT_EQ(result->type, niki::vm::ValueType::Integer);
    EXPECT_EQ(result->as.integer, 0);
}

TEST(DriverProjectTest, ComponentPromotionMissingStructShouldFailBeforeRuntime) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result =
        driver.runProject(resolveCaseDirOrDie("cases/fail/semantic_component_promotion_missing_struct"), options);
    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(result.error().empty());
    EXPECT_NE(niki::diagnostic::renderDiagnosticBagText(result.error()).find("Promoted component source struct not found."),
              std::string::npos);
}

TEST(DriverProjectTest, ComponentMultiPromotionCaseRunsSuccessfully) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/success/10_component_multi_promotion"), options);
    ASSERT_TRUE(result.has_value()) << niki::diagnostic::renderDiagnosticBagText(result.error());
    ASSERT_EQ(result->type, niki::vm::ValueType::Integer);
    EXPECT_EQ(result->as.integer, 0);
}

TEST(DriverProjectTest, ModuleNameMismatchShouldFailDuringImportResolution) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/fail/semantic_module_name_mismatch"), options);
    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(result.error().empty());
    EXPECT_NE(niki::diagnostic::renderDiagnosticBagText(result.error()).find("Imported module not found."),
              std::string::npos);
}

TEST(DriverProjectTest, ModuleBoundaryMayIgnoreSiblingTopLevelDecls) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result =
        driver.runProject(resolveCaseDirOrDie("cases/fail/semantic_module_boundary_ignores_sibling_decl"), options);
    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(result.error().empty());
    EXPECT_NE(niki::diagnostic::renderDiagnosticBagText(result.error()).find("Imported symbol not exported."),
              std::string::npos);
}

TEST(DriverProjectTest, KitsDuplicateAliasShouldFailBeforeRuntime) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/fail/semantic_kits_duplicate_alias"), options);
    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(result.error().empty());
    EXPECT_NE(niki::diagnostic::renderDiagnosticBagText(result.error()).find("Duplicate kits alias in same kits scope."),
              std::string::npos);
}

TEST(DriverProjectTest, KitsUnknownComponentShouldFailBeforeRuntime) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/fail/semantic_kits_unknown_component"), options);
    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(result.error().empty());
    EXPECT_NE(
        niki::diagnostic::renderDiagnosticBagText(result.error())
            .find("Kits target must be a component declared in current module."),
        std::string::npos);
}

