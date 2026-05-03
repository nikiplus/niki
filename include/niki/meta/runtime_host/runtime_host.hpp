#pragma once

#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/linker/linker_facade.hpp"
#include "niki/l0_core/vm/value.hpp"
#include "niki/l0_core/vm/vm.hpp"
#include <expected>

/** @meta_runtime_host_api: 运行宿主策略入口
 * 该接口负责把 `LinkedProgram` 送入 VM 执行，收口 init/entry 阶段错误。
 * 它是 runtime 启动策略层，不参与编译与链接决策。
 */
namespace niki::meta::runtime_host {

struct HostLaunchOptions {
    bool print_init_result = false;
    bool print_entry_result = true;
};

class RuntimeHost {
  public:
    /// @brief 运行链接产物：先 init chunks，再执行 entry 函数。
    std::expected<vm::Value, diagnostic::DiagnosticBag> launch(vm::VM &vm, const linker::LinkedProgram &program,
                                                               const HostLaunchOptions &options);
};

} // namespace niki::meta::runtime_host
