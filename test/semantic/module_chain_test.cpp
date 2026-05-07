#include "niki/l0_core/semantic/type_checker.hpp"
#include "niki/l0_core/syntax/parser.hpp"
#include "niki/l0_core/syntax/scanner.hpp"
#include "niki/meta/precompile/precompile_pipeline.hpp"
#include "niki/l0_core/ir/builder.hpp"
#include <gtest/gtest.h>
#include <string>

using namespace niki;
using namespace niki::syntax;
using namespace niki::semantic;

namespace {

CompilationUnit parseIntoUnit(StringInterner &interner, const std::string &source_path, std::string source) {
    CompilationUnit unit(interner);
    unit.source_path = source_path;
    unit.source = std::move(source);
    Scanner scanner(unit.source, unit.source_path);
    while (true) {
        auto token = scanner.scanToken();
        unit.tokens.push_back(token);
        if (token.type == TokenType::TOKEN_EOF) {
            break;
        }
    }
    static_cast<void>(scanner.takeDiagnostics());
    unit.pool.source_path = unit.source_path;
    Parser parser(unit.source, unit.tokens, unit.pool, unit.source_path);
    auto pr = parser.parse();
    unit.root = pr.root;
    return unit;
}

} // namespace

TEST(ModuleChainTest, DuplicateFileStemRejectsRegistry) {
    StringInterner interner;
    TypeArena arena;
    ModuleNamespace ns;
    ModuleIdAllocator ids;

    auto u1 = parseIntoUnit(interner, "proj/a/util.nk", "module U { export func f()->int { return 1; } }");
    u1.module_id = ids.ensure(u1.source_path);
    auto u2 = parseIntoUnit(interner, "proj/b/util.nk", "module U { export func g()->int { return 2; } }");
    u2.module_id = ids.ensure(u2.source_path);

    ASSERT_TRUE(meta::precompile::predeclareSingleUnit(u1, arena, ns).has_value());
    ASSERT_TRUE(meta::precompile::predeclareSingleUnit(u2, arena, ns).has_value());

    std::vector<CompilationUnit> units;
    units.push_back(std::move(u1));
    units.push_back(std::move(u2));
    auto ctx = meta::precompile::buildModuleSemanticContext(units, ns);
    ASSERT_FALSE(ctx.has_value()) << "Same file stem in different paths must conflict";
}

TEST(ModuleChainTest, ImportResolvesByExplicitModuleNameOrStem) {
    StringInterner interner;
    TypeArena arena;
    ModuleNamespace ns;
    ModuleIdAllocator ids;

    auto lib = parseIntoUnit(interner, "lib/helper.nk",
                             "module helper { export func x()->int { return 41; } }");
    lib.module_id = ids.ensure(lib.source_path);
    auto consumer = parseIntoUnit(
        interner, "app/main.nk",
        "module main { import { x } from helper; func __test_main()->int { return x(); } }");
    consumer.module_id = ids.ensure(consumer.source_path);

    ASSERT_TRUE(meta::precompile::predeclareSingleUnit(lib, arena, ns).has_value());
    ASSERT_TRUE(meta::precompile::predeclareSingleUnit(consumer, arena, ns).has_value());

    std::vector<CompilationUnit> units;
    units.push_back(std::move(lib));
    units.push_back(std::move(consumer));
    auto ctx = meta::precompile::buildModuleSemanticContext(units, ns);
    ASSERT_TRUE(ctx.has_value());

    TypeChecker checker;
    CompilationUnit &consumer_ref = units[1];
    auto chk = checker.check(consumer_ref.pool, consumer_ref.root, arena, ctx->visible_per_unit[1],
                             consumer_ref.module_id, ns);
    EXPECT_TRUE(chk.has_value());
}

TEST(ModuleChainTest, SyntheticWrappedRootLeavesEmptyModuleNameInIR) {
    StringInterner interner;
    CompilationUnit unit = parseIntoUnit(interner, "__wrap__.nk", "func __test_main()->int { return 0; }");
    unit.module_id = 0;
    ir::IRBuilder builder;
    auto ir = builder.build(unit, nullptr);
    ASSERT_TRUE(ir.has_value());
    EXPECT_TRUE(ir->module_name.empty()) << "Synthetic outer module must not set IR logical name";
}

TEST(ModuleChainTest, NestedModuleScopedPredeclareAndImport) {
    StringInterner interner;
    TypeArena arena;
    ModuleNamespace ns;
    std::string src = R"(
module M {
  export func exp()->int { return 7; }
  module Inner {
    import { exp } from M;
    export func g()->int { return exp(); }
  }
  func __test_main()->int { return g(); }
}
)";
    CompilationUnit unit = parseIntoUnit(interner, "single.nk", src);
    unit.module_id = 0;
    ASSERT_TRUE(meta::precompile::predeclareSingleUnit(unit, arena, ns).has_value());
    std::vector<CompilationUnit> units;
    units.push_back(std::move(unit));
    auto ctx = meta::precompile::buildModuleSemanticContext(units, ns);
    ASSERT_TRUE(ctx.has_value());
    TypeChecker checker;
    auto &u = units[0];
    auto chk = checker.check(u.pool, u.root, arena, ctx->visible_per_unit[0], u.module_id, ns);
    EXPECT_TRUE(chk.has_value()) << "Nested module import/predeclare should resolve";
}

TEST(ModuleChainTest, ImportMissingExportFailsInModuleContext) {
    StringInterner interner;
    TypeArena arena;
    ModuleNamespace ns;
    ModuleIdAllocator ids;

    auto lib = parseIntoUnit(interner, "lib/a.nk", "module A { func hidden()->int { return 1; } }");
    lib.module_id = ids.ensure(lib.source_path);
    auto consumer = parseIntoUnit(interner, "app/b.nk",
                                  "module B { import { hidden } from A; func __test_main()->int { return hidden(); } }");
    consumer.module_id = ids.ensure(consumer.source_path);

    ASSERT_TRUE(meta::precompile::predeclareSingleUnit(lib, arena, ns).has_value());
    ASSERT_TRUE(meta::precompile::predeclareSingleUnit(consumer, arena, ns).has_value());

    std::vector<CompilationUnit> units;
    units.push_back(std::move(lib));
    units.push_back(std::move(consumer));
    auto ctx = meta::precompile::buildModuleSemanticContext(units, ns);
    ASSERT_FALSE(ctx.has_value()) << "Non-exported symbol should fail visible resolution";
}
