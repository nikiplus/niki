#pragma once
#include "niki/diagnostic/diagnostic.hpp"
#include "niki/linker/linker.hpp"
#include "niki/vm/value.hpp"
#include "niki/vm/vm.hpp"
#include <expected>

namespace niki::runtime {

struct LaunchOptions {
    // 是否打印每个 init chunk 的执行结果（通常用于调试初始化阶段）。
    bool print_init_result = false;
    // 是否打印入口函数返回值。
    bool print_entry_result = true;
    // 是否调用入口函数；为 false 时只执行初始化段。
    bool call_entry = true;
};

class Launcher {
  public:
    // 启动总入口：
    // 1) 顺序执行链接产物中的 init chunks
    // 2) 可选查找并调用入口函数
    // 3) 返回最终值或启动错误
    std::expected<vm::Value, niki::diagnostic::DiagnosticBag> launchProgram(vm::VM &vm,
                                                                             const linker::LinkedProgram &program,
                                                                             const LaunchOptions options);
};
} // namespace niki::runtime