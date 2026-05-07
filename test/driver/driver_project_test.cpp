#include "../helpers/test_helpers.hpp"
#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/linker/linker_facade.hpp"
#include "niki/l0_core/runtime/launcher.hpp"
#include "niki/l0_core/semantic/module_id.hpp"
#include "niki/l0_core/vm/value.hpp"
#include "niki/l0_core/vm/vm.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace niki;
using namespace niki::ir;
using namespace niki::syntax;
using namespace niki::semantic;
using namespace niki::vm;

/** @phase_D: 多模块/完整管线集成测试 */

static CompilationUnit buildUnitFromSource(
    syntax::StringInterner &interner, const std::string &source, const std::string &source_path,
    ModuleIdAllocator &module_id_allocator) {
    CompilationUnit unit(interner);
    unit.source = source;
    unit.source_path = source_path;
    syntax::Scanner scanner(unit.source, unit.source_path);
    while (true) {
        auto token = scanner.scanToken();
        unit.tokens.push_back(token);
        if (token.type == syntax::TokenType::TOKEN_EOF) break;
    }
    static_cast<void>(scanner.takeDiagnostics());
    unit.pool.source_path = unit.source_path;
    syntax::Parser parser(unit.source, unit.tokens, unit.pool, unit.source_path);
    auto parse_result = parser.parse();
    unit.root = parse_result.root;
    unit.module_id = module_id_allocator.ensure(source_path);
    return unit;
}

// D-1: 单模块直接执行: func main()->int{return 42;}
TEST(DriverProjectTest, SingleModuleExecute) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return 42;");
    ASSERT_TRUE(unit.root.isvalid());

    auto compile_result = meta::orchestrator::compileParsedUnit(unit, fixture.arena_, fixture.module_namespace_);
    ASSERT_TRUE(compile_result.has_value()) << "Compile should succeed";

    linker::Linker linker;
    linker::LinkOptions opts;
    opts.entry_name = "__test_main";
    auto linked = linker.link({compile_result.value()}, opts);
    ASSERT_TRUE(linked.has_value()) << "Link should succeed";

    VM vm;
    runtime::Launcher launcher;
    auto launch_result = launcher.launchProgram(vm, linked.value(), runtime::LaunchOptions{});
    ASSERT_TRUE(launch_result.has_value()) << "Launch should succeed";
    EXPECT_EQ(launch_result.value().type, ValueType::Integer);
    EXPECT_EQ(launch_result.value().as.integer, 42);
}

// D-2: 带变量计算: var x=19; var y=23; return x+y;
TEST(DriverProjectTest, VariableComputation) {
    ExprTestFixture fixture;
    auto val_result = fixture.compileAndRun("var x=19; var y=23; return x+y;");
    ASSERT_TRUE(val_result.has_value());
    EXPECT_EQ(val_result.value().as.integer, 42);
}

// D-2b: 堆局部 string + OP_FREE 全链路不崩溃
TEST(DriverProjectTest, HeapStringLocalFullPipeline) {
    ExprTestFixture fixture;
    auto val_result = fixture.compileAndRun("var s: string = \"ok\"; return 0;");
    ASSERT_TRUE(val_result.has_value()) << "full pipeline with heap local should succeed";
    EXPECT_EQ(val_result.value().type, ValueType::Integer);
    EXPECT_EQ(val_result.value().as.integer, 0);
}

// D-3: 函数调用: func add(a:int,b:int)->int{return a+b;} func main()->int{return add(20,22);}
TEST(DriverProjectTest, FunctionCallWithinModule) {
    ExprTestFixture fixture;
    std::string source =
        "module __t{"
        "func add(a:int,b:int)->int{return a+b;}"
        "func __test_main()->int{return add(20,22);}"
        "}";
    auto unit = buildUnitFromSource(fixture.interner_, source, "__test__", fixture.module_id_allocator_);
    ASSERT_TRUE(unit.root.isvalid());

    auto compile_result = meta::orchestrator::compileParsedUnit(unit, fixture.arena_, fixture.module_namespace_);
    ASSERT_TRUE(compile_result.has_value()) << "Compile should succeed for function call";

    linker::Linker linker;
    linker::LinkOptions opts;
    opts.entry_name = "__test_main";
    auto linked = linker.link({compile_result.value()}, opts);
    ASSERT_TRUE(linked.has_value()) << "Link should succeed";

    VM vm;
    runtime::Launcher launcher;
    auto launch_result = launcher.launchProgram(vm, linked.value(), runtime::LaunchOptions{});
    ASSERT_TRUE(launch_result.has_value()) << "Launch should succeed";
    EXPECT_EQ(launch_result.value().type, ValueType::Integer);
    EXPECT_EQ(launch_result.value().as.integer, 42);
}

// D-4: 多函数调用链: funcA 调用 funcB，funcB 调用 funcC
TEST(DriverProjectTest, MultiFunctionCallChain) {
    ExprTestFixture fixture;
    std::string source =
        "module __t{"
        "func addOne(x:int)->int{return x+1;}"
        "func doubleIt(x:int)->int{return x*2;}"
        "func __test_main()->int{return doubleIt(addOne(20));}"
        "}";
    auto unit = buildUnitFromSource(fixture.interner_, source, "__test__", fixture.module_id_allocator_);
    ASSERT_TRUE(unit.root.isvalid());

    auto compile_result = meta::orchestrator::compileParsedUnit(unit, fixture.arena_, fixture.module_namespace_);
    ASSERT_TRUE(compile_result.has_value()) << "Compile should succeed for multi-function call chain";

    linker::Linker linker;
    linker::LinkOptions opts;
    opts.entry_name = "__test_main";
    auto linked = linker.link({compile_result.value()}, opts);
    ASSERT_TRUE(linked.has_value()) << "Link should succeed";

    VM vm;
    runtime::Launcher launcher;
    auto launch_result = launcher.launchProgram(vm, linked.value(), runtime::LaunchOptions{});
    ASSERT_TRUE(launch_result.has_value()) << "Launch should succeed";
    EXPECT_EQ(launch_result.value().type, ValueType::Integer);
    EXPECT_EQ(launch_result.value().as.integer, 42);
}

// D-5: 完整编译执行复杂表达式
TEST(DriverProjectTest, CompileAndRunComplexExpression) {
    ExprTestFixture fixture;
    auto val_result = fixture.compileAndRun("return ((1+2)*(3+4))/(5%3);");
    ASSERT_TRUE(val_result.has_value());
    EXPECT_EQ(val_result.value().as.integer, 10);
}
