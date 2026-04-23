#include "niki/l0_core/diagnostic/codes.hpp"
#include "niki/l0_core/runtime/launcher.hpp"
#include "niki/l0_core/vm/vm.hpp"
#include <gtest/gtest.h>

using namespace niki::runtime;

TEST(LauncherDiagnosticsTest, EntryLookupFailureProducesDiagnostic) {
    niki::vm::VM vm;
    Launcher launcher;
    LaunchOptions options;

    niki::linker::LinkedProgram program;
    program.entry_name_id = 12345;

    auto result = launcher.launchProgram(vm, program, options);
    ASSERT_FALSE(result.has_value());
    const auto &diagnostics = result.error().all();
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics[0].code, niki::diagnostic::codes::launcher::EntryLookupFailed);
}
