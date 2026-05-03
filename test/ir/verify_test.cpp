#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/ir/verify.hpp"
#include "niki/l0_core/semantic/global_compilation.hpp"
#include "niki/l0_core/semantic/global_symbol_table.hpp"
#include "niki/l0_core/semantic/global_type_arena.hpp"
#include "niki/l0_core/semantic/type_checker.hpp"
#include "niki/l0_core/syntax/global_interner.hpp"
#include "niki/l0_core/syntax/parser.hpp"
#include "niki/l0_core/syntax/scanner.hpp"
#include "niki/meta/precompile/precompile_pipeline.hpp"
#include <gtest/gtest.h>

using namespace niki;
using namespace niki::ir;

namespace {
std::expected<ModuleIR, diagnostic::DiagnosticBag> buildIRFromBody(std::string_view body) {
    syntax::GlobalInterner interner;
    GlobalTypeArena arena;
    GlobalSymbolTable symbols;

    GlobalCompilationUnit unit(interner);
    unit.source_path = "__verify_test__";
    unit.source = "module __t{func __test_main()->int{" + std::string(body) + "}}";

    syntax::Scanner scanner(unit.source, unit.source_path);
    while (true) {
        auto token = scanner.scanToken();
        unit.tokens.push_back(token);
        if (token.type == syntax::TokenType::TOKEN_EOF) {
            break;
        }
    }
    auto scan_diags = scanner.takeDiagnostics();
    if (!scan_diags.empty()) {
        return std::unexpected(std::move(scan_diags));
    }

    unit.pool.source_path = unit.source_path;
    syntax::Parser parser(unit.source, unit.tokens, unit.pool, unit.source_path);
    auto parse_result = parser.parse();
    if (!parse_result.diagnostics.empty()) {
        return std::unexpected(std::move(parse_result.diagnostics));
    }
    unit.root = parse_result.root;

    auto predeclare = meta::precompile::predeclareSingleUnit(unit, arena, symbols);
    if (!predeclare.has_value()) {
        return std::unexpected(std::move(predeclare.error()));
    }

    semantic::TypeChecker checker;
    auto type_result = checker.check(unit.pool, unit.root, symbols, arena);
    if (!type_result.has_value()) {
        return std::unexpected(std::move(type_result.error()));
    }

    IRBuilder builder;
    return builder.build(unit);
}
} // namespace

/** @verify_test: IR 校验阶段测试 */

TEST(IRVerifyTest, EmptyModulePasses) {
    ModuleIR mod;
    auto report = verifyModuleIRFlat(mod);
    EXPECT_TRUE(report.ok());
}

TEST(IRVerifyTest, MinimalIRPasses) {
    auto ir_result = buildIRFromBody("return 42;");
    ASSERT_TRUE(ir_result.has_value());
    auto report = verifyModuleIRFlat(ir_result.value());
    EXPECT_TRUE(report.ok());
}

TEST(IRVerifyTest, ComplexIRPasses) {
    auto ir_result = buildIRFromBody("var a = 1 + 2; var b = a * 3; return b;");
    ASSERT_TRUE(ir_result.has_value());
    auto report = verifyModuleIRFlat(ir_result.value());
    EXPECT_TRUE(report.ok());
}
