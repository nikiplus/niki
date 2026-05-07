#pragma once

#include "niki/debug/logger.hpp"
#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/ir/builder.hpp"
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/linker/linker_facade.hpp"
#include "niki/l0_core/runtime/launcher.hpp"
#include "niki/l0_core/semantic/compilation_unit.hpp"
#include "niki/l0_core/semantic/type_arena.hpp"
#include "niki/l0_core/semantic/module_id.hpp"
#include "niki/l0_core/semantic/module_namespace.hpp"
#include "niki/l0_core/semantic/type_checker.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/string_interner.hpp"
#include "niki/l0_core/syntax/parser.hpp"
#include "niki/l0_core/syntax/scanner.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include "niki/l0_core/vm/value.hpp"
#include "niki/l0_core/vm/vm.hpp"
#include "niki/meta/orchestrator/compile_pipeline.hpp"
#include "niki/meta/precompile/precompile_pipeline.hpp"
#include <expected>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

/** @test_helpers: 表达式链路测试共享夹具
 *
 * NIKI 语法要求表达式只能存在于函数体中，函数只能存在于模块中。
 * 因此测试不能"裸测一个表达式"，必须构造完整的包裹结构。
 *
 * ExprTestFixture 提供：
 *   wrapAndParse()   - 将裸语句体包裹为完整编译单元
 *   runTypeCheck()   - 执行预声明+类型检查
 *   buildIR()        - 执行类型检查+IR构建
 *   compileAndRun()  - 通过 compileParsedUnit（预声明+可见性+类型检查+后端）完整编译执行并返回结果
 *   findNodes()      - AST 节点查找辅助
 */

using namespace niki;
using namespace niki::syntax;
using namespace niki::semantic;
using namespace niki::ir;

class ExprTestFixture {
  public:
    ExprTestFixture() : interner_(), arena_(), module_namespace_() { ensureLoggerInitialized(); }

    //--------------------------------------------------------------
    // 1. 包裹器：将裸语句体包装为完整的编译单元
    //    输入: "var x = 42; return x;"
    //    输出: "module __t{func __test_main()->int{var x = 42; return x;}}"
    //--------------------------------------------------------------
    CompilationUnit wrapAndParse(std::string_view body, std::string_view return_type_hint = "int") {
        std::string source = buildWrappedSource(body, return_type_hint);

        CompilationUnit unit(interner_);
        unit.source = source;
        unit.source_path = "__test__";
        unit.module_id = module_id_allocator_.ensure(unit.source_path);

        // Scan tokens
        syntax::Scanner scanner(unit.source, unit.source_path);
        while (true) {
            auto token = scanner.scanToken();
            unit.tokens.push_back(token);
            if (token.type == syntax::TokenType::TOKEN_EOF)
                break;
        }
        // Accept scanner diagnostics (may be empty)
        static_cast<void>(scanner.takeDiagnostics());

        // Parse
        unit.pool.source_path = unit.source_path;
        syntax::Parser parser(unit.source, unit.tokens, unit.pool, unit.source_path);
        auto parse_result = parser.parse();
        unit.root = parse_result.root;
        return unit;
    }

    //--------------------------------------------------------------
    // 2. 预声明 + 类型检查
    //--------------------------------------------------------------
    std::expected<TypeCheckResult, diagnostic::DiagnosticBag> runTypeCheck(CompilationUnit &unit) {
        debug::trace("test_helper", "runTypeCheck: predeclare start");
        // 预声明：将 __test_main 注册到全局符号表
        auto predeclare = meta::precompile::predeclareSingleUnit(unit, arena_, module_namespace_);
        if (!predeclare.has_value()) {
            debug::trace("test_helper", "runTypeCheck: predeclare FAILED");
            return std::unexpected(std::move(predeclare.error()));
        }
        debug::trace("test_helper", "runTypeCheck: predeclare OK, typecheck start");

        // 类型检查
        TypeChecker checker;
        auto result = checker.check(unit.pool, unit.root, arena_, unit.module_id, module_namespace_);
        if (!result.has_value()) {
            debug::trace("test_helper", "runTypeCheck: typecheck FAILED");
        } else {
            debug::trace("test_helper", "runTypeCheck: typecheck OK");
        }
        return result;
    }

    //--------------------------------------------------------------
    // 3. 预声明 + 类型检查 + IR 构建
    //--------------------------------------------------------------
    std::expected<ModuleIR, diagnostic::DiagnosticBag> buildIR(CompilationUnit &unit) {
        debug::trace("test_helper", "buildIR: typecheck start");
        auto type_result = runTypeCheck(unit);
        if (!type_result.has_value()) {
            debug::trace("test_helper", "buildIR: typecheck FAILED");
            return std::unexpected(std::move(type_result.error()));
        }
        debug::trace("test_helper", "buildIR: typecheck OK, building IR");

        IRBuilder builder;
        auto ir_result = builder.build(unit, nullptr);  // 测试环境无跨模块 visible symbols
        if (!ir_result.has_value()) {
            debug::trace("test_helper", "buildIR: IR build FAILED");
        } else {
            debug::trace("test_helper", "buildIR: IR build OK, funcs={}, blocks={}, insts={}",
                         ir_result.value().funcs.size(), ir_result.value().blocks.size(),
                         ir_result.value().insts.size());
        }
        return ir_result;
    }

    //--------------------------------------------------------------
    // 4. 完整编译管线：parse -> predeclare -> typecheck -> ir -> link -> run
    //--------------------------------------------------------------
    std::expected<vm::Value, diagnostic::DiagnosticBag> compileAndRun(std::string_view body,
                                                                      std::string_view return_type_hint = "int") {
        debug::trace("test_helper", "compileAndRun: wrapAndParse");
        auto unit = wrapAndParse(body, return_type_hint);
        if (!unit.root.isvalid()) {
            debug::trace("test_helper", "compileAndRun: wrapAndParse returned invalid root");
            diagnostic::DiagnosticBag bag;
            bag.error(diagnostic::events::DriverCode::NoInput, "wrapAndParse returned invalid root");
            return std::unexpected(std::move(bag));
        }

        debug::trace("test_helper", "compileAndRun: compileParsedUnit start");
        // 完整语义 + 后端（predeclare → 模块可见性 → typecheck → IR/verify/lower）
        auto compile_result = meta::orchestrator::compileParsedUnit(unit, arena_, module_namespace_);
        if (!compile_result.has_value()) {
            debug::trace("test_helper", "compileAndRun: compileParsedUnit FAILED");
            return std::unexpected(std::move(compile_result.error()));
        }
        debug::trace("test_helper", "compileAndRun: compileParsedUnit OK");

        // Link
        debug::trace("test_helper", "compileAndRun: linking");
        linker::Linker linker;
        linker::LinkOptions link_opts;
        link_opts.entry_name = "__test_main";
        auto linked = linker.link({compile_result.value()}, link_opts);
        if (!linked.has_value()) {
            debug::trace("test_helper", "compileAndRun: link FAILED");
            return std::unexpected(std::move(linked.error()));
        }
        debug::trace("test_helper", "compileAndRun: link OK");

        // Launch
        debug::trace("test_helper", "compileAndRun: launching");
        vm::VM vm;
        runtime::Launcher launcher;
        auto launch_result = launcher.launchProgram(vm, linked.value(), runtime::LaunchOptions{});
        if (!launch_result.has_value()) {
            debug::trace("test_helper", "compileAndRun: launch FAILED");
            return std::unexpected(std::move(launch_result.error()));
        }
        debug::trace("test_helper", "compileAndRun: DONE, result_type={}",
                     static_cast<int>(launch_result.value().type));

        return launch_result.value();
    }

    //--------------------------------------------------------------
    // 5. AST 节点查找辅助
    //--------------------------------------------------------------
    std::vector<size_t> findNodes(const syntax::ASTPool &pool, NodeType type) const {
        std::vector<size_t> indices;
        for (size_t i = 0; i < pool.nodes.size(); i++) {
            if (pool.nodes[i].type == type)
                indices.push_back(i);
        }
        return indices;
    }

    syntax::ASTNodeIndex findFirstNode(const syntax::ASTPool &pool, NodeType type) const {
        for (size_t i = 0; i < pool.nodes.size(); i++) {
            if (pool.nodes[i].type == type)
                return syntax::ASTNodeIndex{static_cast<uint32_t>(i)};
        }
        return syntax::ASTNodeIndex::invalid();
    }

    // 共享基础设施
    syntax::StringInterner interner_;
    TypeArena arena_;
    ModuleNamespace module_namespace_;
    ModuleIdAllocator module_id_allocator_;

  private:
    static void ensureLoggerInitialized() {
        static std::once_flag flag;
        std::call_once(flag, []() { debug::initLogger(spdlog::level::trace, "logs/niki_test.log"); });
    }

    static std::string buildWrappedSource(std::string_view body, std::string_view return_type_hint) {
        return "module __t{func __test_main()->" + std::string(return_type_hint) + "{" + std::string(body) + "}}";
    }
};
