#pragma once
#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/linker/linker.hpp"
#include "niki/l0_core/vm/value.hpp"
#include "niki/l0_core/vm/vm.hpp"
#include <expected>

/** @launcher: 链接产物到执行引擎的启动编排层
 * 这个头文件定义 runtime 启动阶段的对外契约。它不参与语法、语义、IR、链接本身，
 * 只负责把 `LinkedProgram` 正确、可诊断地送进 VM 执行。
 *
 * 从运行时架构看，启动并不是一次“直接调用 main”这么简单，至少包含三个步骤：
 * 1) 装载链接产物；
 * 2) 顺序执行模块初始化块（init chunks）；
 * 3) 决议入口并执行入口函数。
 * 任何一步失败都属于“启动失败”而非“编译失败”，因此要在 runtime 层统一映射为诊断输出。
 *
 * `LaunchOptions` 的存在意味着启动阶段本身也是可观测的工程接口：
 * 调试时可以输出 init 结果与入口结果，线上/CI 又可关闭冗余输出保持稳定日志。
 * 这种“行为可配置但契约稳定”的设计，能避免把调试逻辑散落到 VM 执行核心。
 *
 * 总结来说，launcher.hpp 把项目级程序对象与执行器连接起来，
 * 提供了清晰的运行边界和错误边界：成功给出最终 Value，失败给出结构化 DiagnosticBag。
 */
namespace niki::runtime {

struct LaunchOptions {
    // 是否打印每个 init chunk 的执行结果（通常用于调试初始化阶段）。
    bool print_init_result = false;
    // 是否打印入口函数返回值。
    bool print_entry_result = true;
};

class Launcher {
  public:
    // 启动总入口：
    // 1) 顺序执行链接产物中的 init chunks
    // 2) 查找并调用入口函数
    // 3) 返回最终值或启动错误
    std::expected<vm::Value, niki::diagnostic::DiagnosticBag> launchProgram(vm::VM &vm,
                                                                            const linker::LinkedProgram &program,
                                                                            const LaunchOptions options);
};
} // namespace niki::runtime