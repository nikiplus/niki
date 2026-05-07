#pragma once

#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/linker/linker_facade.hpp"
#include "niki/l0_core/semantic/compilation_unit.hpp"
#include "niki/l0_core/semantic/type_arena.hpp"
#include "niki/l0_core/semantic/module_namespace.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <expected>
#include <string>
#include <vector>

/** @meta_orchestrator_api: 项目级编排门面
 * 该头文件定义元编排层对外入口：负责“阶段串联”和“工程级错误收口”，
 * 不承载 IR 发射/字节码编码等后端细节实现。
 *
 * 分层关系：
 * - 调用 `meta::precompile` 完成 parse/predeclare/module context。
 * - 调用 compile pipeline 完成单单元 IR->chunk 编译。
 * - 调用 l0 linker/runtime 进入链接与运行阶段。
 */
namespace niki::meta::orchestrator {

struct OrchestratorOptions {
    bool recursive_scan = true;
    std::string file_ext = ".nk";
    std::string entry_name = "main";
};

class CompilerOrchestrator {
  public:
    /// @brief 编排入口：项目扫描、编译、链接、运行。
    std::expected<vm::Value, diagnostic::DiagnosticBag> runProject(const std::string &root_dir,
                                                                   const OrchestratorOptions &options);

  private:
    /// @brief 收集项目下待编译 `.nk` 文件列表。
    std::vector<std::string> collectNkFiles(const std::string &root_dir, const OrchestratorOptions &options);
    /// @brief 读取并解析单源文件到 CompilationUnit。
    std::expected<CompilationUnit, diagnostic::DiagnosticBag> parseOneUnit(const std::string &source_path,
                                                                                 syntax::StringInterner &interner);
    /// @brief 执行项目级多文件编译总流程（不含链接与运行）。
    std::expected<std::vector<linker::CompileModule>, diagnostic::DiagnosticBag> compileAll(
        const std::vector<std::string> &files);
    /// @brief 对全部 unit 执行预声明阶段并汇总诊断。
    std::expected<void, diagnostic::DiagnosticBag> predeclareAllUnits(const std::vector<CompilationUnit> &units,
                                                                      TypeArena &global_arena,
                                                                      ModuleNamespace &module_namespace);
};

} // namespace niki::meta::orchestrator
