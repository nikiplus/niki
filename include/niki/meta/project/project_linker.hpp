#pragma once

#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/linker/linker.hpp"
#include <expected>
#include <vector>

/** @meta_project_linker_api: 项目级链接策略入口
 * 该接口位于元编排层，负责把多个 `CompileModule` 组装为单一 `LinkedProgram`。
 * 它承载的是工程策略（入口决议、重复符号检查、模块合并），而非 IR 构建语义。
 */
namespace niki::meta::project {

class ProjectLinker {
  public:
    /// @brief 执行项目级链接并返回可运行程序或诊断。
    std::expected<linker::LinkedProgram, diagnostic::DiagnosticBag> link(const std::vector<linker::CompileModule> &modules,
                                                                          const linker::LinkOptions &options);
};

} // namespace niki::meta::project
