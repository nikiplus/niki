#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/linker/linker_facade.hpp"
#include "niki/l0_core/semantic/module_id.hpp"
#include "niki/l0_core/vm/object.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <gtest/gtest.h>

using namespace niki::linker;

namespace {

CompileModule makeModuleWithFunction(const std::string &name, const std::string &path, niki::ModuleId module_id) {
    CompileModule module;
    module.module_id = module_id;
    module.module_name = name;
    module.source_path = path;

    module.init_chunk.module_id = module_id;
    module.init_chunk.string_pool.push_back(name);
    auto *function_object = new niki::vm::ObjFunction();
    function_object->object_header.type = niki::vm::ObjType::Function;
    function_object->name_id = 0;
    module.init_chunk.constants.push_back(niki::vm::Value::makeObject(function_object));
    module.exports[0] = 0;
    return module;
}

} // namespace

TEST(LinkerDiagnosticsTest, DuplicateSymbolProducesDiagnostic) {
    Linker linker;
    LinkOptions options;
    options.entry_name = "main";

    std::vector<CompileModule> modules;
    // 同一 module_id 下重复导出同名符号 → DuplicateSymbol（跨模块同名见阶段 I，需不同 module_id）
    modules.push_back(makeModuleWithFunction("dup", "a.nk", 0));
    modules.push_back(makeModuleWithFunction("dup", "b.nk", 0));

    auto result = linker.link(modules, options);
    ASSERT_FALSE(result.has_value());
    const auto &diagnostics = result.error().all();
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics[0].code, niki::diagnostic::codeOf(niki::diagnostic::events::LinkerCode::DuplicateSymbol));
    EXPECT_EQ(diagnostics[0].stage, niki::diagnostic::DiagnosticStage::Linker);
    EXPECT_EQ(diagnostics[0].severity, niki::diagnostic::DiagnosticSeverity::Error);
    EXPECT_FALSE(diagnostics[0].message.empty());
}

TEST(LinkerDiagnosticsTest, MissingEntryProducesDiagnostic) {
    Linker linker;
    LinkOptions options;
    options.entry_name = "main";

    std::vector<CompileModule> modules;
    modules.push_back(makeModuleWithFunction("helper", "helper.nk", 1));

    auto result = linker.link(modules, options);
    ASSERT_FALSE(result.has_value());
    const auto &diagnostics = result.error().all();
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics[0].code, niki::diagnostic::codeOf(niki::diagnostic::events::LinkerCode::EntryNotFound));
    EXPECT_EQ(diagnostics[0].stage, niki::diagnostic::DiagnosticStage::Linker);
    EXPECT_EQ(diagnostics[0].severity, niki::diagnostic::DiagnosticSeverity::Error);
    EXPECT_FALSE(diagnostics[0].message.empty());
}
