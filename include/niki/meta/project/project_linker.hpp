#pragma once

#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/linker/linker_facade.hpp"
#include <expected>
#include <vector>

/** @meta_project_linker_api: 项目级链接（唯一标准实现）
 *
 * 链接阶段处理的是拿到全部编译模块之后才成立的全局一致性问题（非局部语法或类型语义）：
 * - 是否存在重复导出符号；
 * - 入口函数是否存在且唯一；
 * - 如何把各模块的产物组织成 runtime 可直接消费的 `LinkedProgram`（初始化块集合、字符串池等）。
 *
 * `CompileModule` 与 `LinkedProgram` 的契约定义在 `niki/l0_core/linker/linker_facade.hpp`，
 * 便于编译产物与运行时层保持稳定的头文件耦合面。
 *
 * `niki::linker::Linker` 仅门面转发到本类型，编排与业务侧应尽量通过该门面调用，避免散落依赖 meta 路径。
 *
 * MVP 聚焦于可联编、可运行的符号与入口决议；日后的字符池重映射、操作数重定位、初始化块深层合并等演进应在此处落地。
 */
namespace niki::meta::project {

class ProjectLinker {
  public:
    /// @brief 执行项目级链接并返回可运行程序或诊断。
    std::expected<linker::LinkedProgram, diagnostic::DiagnosticBag> link(
        const std::vector<linker::CompileModule> &modules, const linker::LinkOptions &options);
};

} // namespace niki::meta::project
