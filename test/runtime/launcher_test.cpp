#include "niki/diagnostic/codes.hpp"
#include "niki/runtime/launcher.hpp"
#include "niki/vm/vm.hpp"
#include <gtest/gtest.h>

using namespace niki::runtime;

TEST(LauncherDiagnosticsTest, EntryLookupFailureProducesDiagnostic) {
    niki::vm::VM vm;
    Launcher launcher;
    LaunchOptions options;
    options.call_entry = true;

    niki::linker::LinkedProgram program;
    program.entry_name_id = 12345;

    auto result = launcher.launchProgram(vm, program, options);
    ASSERT_FALSE(result.has_value());
    const auto &diagnostics = result.error().all();
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics[0].code, niki::diagnostic::codes::launcher::EntryLookupFailed);
}

TEST(LauncherDiagnosticsTest, InitOnlyModeReturnsLastInitResult) {
    niki::vm::VM vm;
    Launcher launcher;
    LaunchOptions options;
    options.call_entry = false;

    niki::linker::LinkedProgram program;
    auto result = launcher.launchProgram(vm, program, options);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().type, niki::vm::ValueType::Nil);
}
