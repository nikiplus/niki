#include "niki/l0_core/runtime/launcher.hpp"
#include "niki/l0_core/vm/object.hpp"

/** @launcher_impl: LinkedProgram 装载与 VM 执行
 * 顺序执行 init chunks，解析入口并执行入口函数；失败映射为 Launcher 诊断码。
 */
namespace niki::runtime {

std::expected<vm::Value, niki::diagnostic::DiagnosticBag> Launcher::launchProgram(vm::VM &vm,
                                                                                  const linker::LinkedProgram &program,
                                                                                  const LaunchOptions options) {
    for (const auto &chunk : program.init_chunks) {
        auto init_ret = vm.executeChunk(chunk, options.print_init_result);
        if (!init_ret.has_value()) {
            niki::diagnostic::DiagnosticBag diagnostics;
            diagnostics.error(niki::diagnostic::events::LauncherCode::InitRuntimeError, "Initialization failed.");
            return std::unexpected(std::move(diagnostics));
        }
    }

    vm::ObjFunction *entry = vm.lookupGlobalFunctionById(program.entry_module_id, program.entry_name_id);
    if (entry == nullptr) {
        niki::diagnostic::DiagnosticBag diagnostics;
        diagnostics.error(niki::diagnostic::events::LauncherCode::EntryLookupFailed, "Entry function not found.");
        return std::unexpected(std::move(diagnostics));
    }

    auto entry_ret = vm.executeFunction(entry, options.print_entry_result);
    if (!entry_ret.has_value()) {
        niki::diagnostic::DiagnosticBag diagnostics;
        diagnostics.error(niki::diagnostic::events::LauncherCode::EntryRuntimeError, "Entry function execution failed.");
        return std::unexpected(std::move(diagnostics));
    }

    return entry_ret.value();
}

} // namespace niki::runtime
