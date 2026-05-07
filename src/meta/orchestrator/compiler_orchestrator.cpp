#include "niki/meta/orchestrator/compiler_orchestrator.hpp"
#include "niki/l0_core/linker/linker_facade.hpp"
#include "niki/l0_core/runtime/launcher.hpp"
#include "niki/l0_core/semantic/module_id.hpp"
#include "niki/l0_core/semantic/type_checker.hpp"
#include "niki/l0_core/syntax/string_interner.hpp"
#include "niki/l0_core/vm/vm.hpp"
#include "niki/meta/orchestrator/compile_pipeline.hpp"
#include "niki/meta/precompile/precompile_pipeline.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

/** @meta_orchestrator_impl: 元编排层主流程实现
 * 该文件承担项目级“流程编排”职责：文件收集、阶段调度、诊断聚合、链接与运行串联。
 * 后端编译细节已下沉到 `compile_pipeline.cpp`，本文件保持控制流语义清晰。
 */
namespace niki::meta::orchestrator {
namespace fs = std::filesystem;
namespace {

static diagnostic::DiagnosticBag makeDriverError(diagnostic::events::DriverCode code, std::string message,
                                                 std::string file = "") {
    diagnostic::DiagnosticBag bag;
    bag.error(code, std::move(message), diagnostic::makeSourceSpan(std::move(file)));
    return bag;
}

} // namespace

/**
 * @brief 扫描项目目录并收集待编译 `.nk` 文件。
 * @param root_dir 项目根目录。
 * @param options 扫描配置（递归/扩展名）。
 * @return std::vector<std::string> 已排序的源文件路径列表。
 */
std::vector<std::string> CompilerOrchestrator::collectNkFiles(const std::string &root_dir,
                                                              const OrchestratorOptions &options) {
    std::vector<std::string> files;
    std::error_code err;
    fs::path root(root_dir);
    if (!fs::exists(root, err) || !fs::is_directory(root, err)) {
        return files;
    }
    auto accept = [&](const fs::directory_entry &entry) {
        return entry.is_regular_file() && entry.path().extension().string() == options.file_ext;
    };
    if (options.recursive_scan) {
        for (fs::recursive_directory_iterator it(root, err), end; it != end && !err; it.increment(err)) {
            if (!err && accept(*it)) {
                files.push_back(it->path().string());
            }
        }
    } else {
        for (fs::directory_iterator it(root, err), end; it != end && !err; it.increment(err)) {
            if (!err && accept(*it)) {
                files.push_back(it->path().string());
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/**
 * @brief 读取并解析单源文件到编译单元。
 * @param source_path 源文件路径。
 * @param interner 项目级字符串驻留器。
 * @return std::expected<CompilationUnit, diagnostic::DiagnosticBag> 成功返回编译单元，失败返回诊断。
 */
std::expected<CompilationUnit, diagnostic::DiagnosticBag> CompilerOrchestrator::parseOneUnit(
    const std::string &source_path, syntax::StringInterner &interner) {
    CompilationUnit unit(interner);
    unit.source_path = source_path;
    std::ifstream in(source_path, std::ios::binary);
    if (!in.is_open()) {
        return std::unexpected(
            makeDriverError(diagnostic::events::DriverCode::IoError, "Failed to open source file.", source_path));
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    unit.source = buffer.str();
    auto parsed = meta::precompile::parseIntoCompilationUnit(unit);
    if (!parsed.has_value()) {
        return std::unexpected(std::move(parsed.error()));
    }
    return unit;
}

/**
 * @brief 对全部编译单元执行预声明并聚合错误。
 * @param units 编译单元列表。
 * @param global_arena 全局类型 arena。
 * @param global_symbols 全局符号表。
 * @return std::expected<void, diagnostic::DiagnosticBag> 成功返回空，失败返回聚合诊断。
 */
std::expected<void, diagnostic::DiagnosticBag> CompilerOrchestrator::predeclareAllUnits(
    const std::vector<CompilationUnit> &units, TypeArena &global_arena, ModuleNamespace &module_namespace) {
    diagnostic::DiagnosticBag diagnostics;
    for (const auto &unit : units) {
        auto one = meta::precompile::predeclareSingleUnit(unit, global_arena, module_namespace);
        if (!one.has_value()) {
            diagnostics.merge(std::move(one.error()));
        }
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return {};
}

/**
 * @brief 执行多文件编译主流程（parse/predeclare/typecheck/backend）。
 * @param files 待编译文件路径集合。
 * @return std::expected<std::vector<linker::CompileModule>, diagnostic::DiagnosticBag> 成功返回模块产物，失败返回诊断。
 */
std::expected<std::vector<linker::CompileModule>, diagnostic::DiagnosticBag> CompilerOrchestrator::compileAll(
    const std::vector<std::string> &files) {
    std::vector<linker::CompileModule> modules;
    diagnostic::DiagnosticBag diagnostics;
    syntax::StringInterner interner;
    TypeArena global_arena;
    ModuleNamespace module_namespace;
    ModuleIdAllocator module_id_allocator;
    std::vector<CompilationUnit> units;
    units.reserve(files.size());
    for (const auto &file : files) {
        auto unit_result = parseOneUnit(file, interner);
        if (!unit_result.has_value()) {
            diagnostics.merge(std::move(unit_result.error()));
            continue;
        }
        auto &unit = unit_result.value();
        unit.module_id = module_id_allocator.ensure(unit.source_path);
        units.push_back(std::move(unit));
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    auto predeclare_result = predeclareAllUnits(units, global_arena, module_namespace);
    if (!predeclare_result.has_value()) {
        diagnostics.merge(std::move(predeclare_result.error()));
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    auto semantic_context_result = meta::precompile::buildModuleSemanticContext(units, module_namespace);
    if (!semantic_context_result.has_value()) {
        diagnostics.merge(std::move(semantic_context_result.error()));
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    auto &visible_per_unit = semantic_context_result.value().visible_per_unit;
    for (size_t unit_idx = 0; unit_idx < units.size(); ++unit_idx) {
        auto &unit = units[unit_idx];
        semantic::TypeChecker checker;
        auto result = checker.check(unit.pool, unit.root, global_arena, visible_per_unit[unit_idx], unit.module_id,
                                    module_namespace);
        if (!result.has_value()) {
            diagnostics.merge(std::move(result.error()));
        }
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    modules.reserve(units.size());
    for (size_t unit_idx = 0; unit_idx < units.size(); ++unit_idx) {
        auto &unit = units[unit_idx];
        auto module_result = compileParsedBackend(unit, global_arena, &visible_per_unit[unit_idx]);
        if (!module_result.has_value()) {
            diagnostics.merge(std::move(module_result.error()));
            continue;
        }
        modules.push_back(std::move(module_result.value()));
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return modules;
}

/**
 * @brief 项目运行入口：编译、链接并在 VM 执行。
 * @param root_dir 项目根目录。
 * @param options 编排选项。
 * @return std::expected<vm::Value, diagnostic::DiagnosticBag> 成功返回入口值，失败返回诊断。
 */
std::expected<vm::Value, diagnostic::DiagnosticBag> CompilerOrchestrator::runProject(
    const std::string &root_dir, const OrchestratorOptions &options) {
    auto files = collectNkFiles(root_dir, options);
    if (files.empty()) {
        return std::unexpected(
            makeDriverError(diagnostic::events::DriverCode::NoInput, "No .nk source files found.", root_dir));
    }
    auto compiled = compileAll(files);
    if (!compiled.has_value()) {
        return std::unexpected(compiled.error());
    }
    linker::Linker linker;
    linker::LinkOptions link_options;
    link_options.entry_name = options.entry_name;
    auto linked = linker.link(compiled.value(), link_options);
    if (!linked.has_value()) {
        return std::unexpected(std::move(linked.error()));
    }
    vm::VM vm;
    runtime::Launcher launcher;
    runtime::LaunchOptions launch_options;
    auto launch_result = launcher.launchProgram(vm, linked.value(), launch_options);
    if (!launch_result.has_value()) {
        return std::unexpected(std::move(launch_result.error()));
    }
    return launch_result.value();
}

} // namespace niki::meta::orchestrator
