#include "niki/driver/driver.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace {
std::string resolveCaseDirOrDie(const std::string &relative_case_path) {
    const std::string path_a = "scripts/" + relative_case_path;
    const std::string path_b = "../scripts/" + relative_case_path;
    if (std::filesystem::exists(path_a)) {
        return path_a;
    }
    if (std::filesystem::exists(path_b)) {
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
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->type, niki::vm::ValueType::Integer);
    EXPECT_EQ(result->as.integer, 42);
}

TEST(DriverProjectTest, MultiFileInitOrderCaseRunsSuccessfully) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/success/02_init_order"), options);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->type, niki::vm::ValueType::Integer);
    EXPECT_EQ(result->as.integer, 77);
}

TEST(DriverProjectTest, MultiDeclStableCaseRunsSuccessfully) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/success/03_multi_decl_stable"), options);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->type, niki::vm::ValueType::Integer);
    EXPECT_EQ(result->as.integer, 100);
}

TEST(DriverProjectTest, DiceBasicCaseRunsSuccessfully) {
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = false;
    options.entry_name = "main";

    auto result = driver.runProject(resolveCaseDirOrDie("cases/success/04_dice_basic"), options);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->type, niki::vm::ValueType::Integer);
    // 0d100+2d1+3d1 == 5；1d6 ∈ [1,6] ⇒ 总和 ∈ [6,11]
    EXPECT_GE(result->as.integer, 6);
    EXPECT_LE(result->as.integer, 11);
}

