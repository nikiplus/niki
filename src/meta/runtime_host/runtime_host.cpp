#include "niki/meta/runtime_host/runtime_host.hpp"
#include "niki/l0_core/vm/object.hpp"

/** @meta_runtime_host_impl: 运行宿主策略实现
 * 该文件实现“初始化块执行 + 入口函数执行”的宿主控制流程，
 * 统一把 VM 执行失败映射为 launcher 诊断事件。
 */
namespace niki::meta::runtime_host {

//------------------------------------------------------------------------------
// HOST_LAUNCH_STAGE: 程序启动执行主流程。
//------------------------------------------------------------------------------
std::expected<vm::Value, diagnostic::DiagnosticBag> RuntimeHost::launch(vm::VM &vm, const linker::LinkedProgram &program,
                                                                        const HostLaunchOptions &options) {
    for (const auto &chunk : program.init_chunks) {
        auto init_ret = vm.executeChunk(chunk, options.print_init_result);
        if (!init_ret.has_value()) {
            diagnostic::DiagnosticBag diagnostics;
            diagnostics.error(diagnostic::events::LauncherCode::InitRuntimeError, "Initialization failed.");
            return std::unexpected(std::move(diagnostics));
        }
    }

    vm::ObjFunction *entry = vm.lookupGlobalFunctionById(program.entry_name_id);
    if (entry == nullptr) {
        diagnostic::DiagnosticBag diagnostics;
        diagnostics.error(diagnostic::events::LauncherCode::EntryLookupFailed, "Entry function not found.");
        return std::unexpected(std::move(diagnostics));
    }

    auto entry_ret = vm.executeFunction(entry, options.print_entry_result);
    if (!entry_ret.has_value()) {
        diagnostic::DiagnosticBag diagnostics;
        diagnostics.error(diagnostic::events::LauncherCode::EntryRuntimeError, "Entry function execution failed.");
        return std::unexpected(std::move(diagnostics));
    }

    return entry_ret.value();
}

} // namespace niki::meta::runtime_host
