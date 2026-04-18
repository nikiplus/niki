#include "niki/diagnostic/codes.hpp"
#include "niki/linker/linker.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/value.hpp"
#include <gtest/gtest.h>

using namespace niki::linker;

namespace {

CompileModule makeModuleWithFunction(const std::string &name, const std::string &path) {
    CompileModule module;
    module.module_name = name;
    module.source_path = path;

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
    options.allow_no_entry = true;

    std::vector<CompileModule> modules;
    modules.push_back(makeModuleWithFunction("dup", "a.nk"));
    modules.push_back(makeModuleWithFunction("dup", "b.nk"));

    auto result = linker.link(modules, options);
    ASSERT_FALSE(result.has_value());
    const auto &diagnostics = result.error().all();
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics[0].code, niki::diagnostic::codes::linker::DuplicateSymbol);
}

TEST(LinkerDiagnosticsTest, MissingEntryProducesDiagnostic) {
    Linker linker;
    LinkOptions options;
    options.entry_name = "main";
    options.allow_no_entry = false;

    std::vector<CompileModule> modules;
    modules.push_back(makeModuleWithFunction("helper", "helper.nk"));

    auto result = linker.link(modules, options);
    ASSERT_FALSE(result.has_value());
    const auto &diagnostics = result.error().all();
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics[0].code, niki::diagnostic::codes::linker::EntryNotFound);
}
