#include "niki/l0_core/runtime/launcher.hpp"
#include "niki/l0_core/vm/object.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <expected>


namespace niki::runtime {

std::expected<vm::Value, niki::diagnostic::DiagnosticBag> Launcher::launchProgram(vm::VM &vm,
                                                                                  const linker::LinkedProgram &program,
                                                                                  const LaunchOptions options) {
    // 启动流程分两段：
    // A. 执行所有模块初始化代码
    // B. 调用入口函数并返回结果

    // 1) 执行链接产物中的初始化段（按 linker 给出的顺序）。
    // 底层视角：
    // - 这一步相当于“模块装载后的初始化提交”，确保全局函数/结构体等定义进入 VM 全局表。
    // - 初始化顺序可观测，因此与链接顺序保持一致能降低时序歧义。
    for (const auto &chunk : program.init_chunks) {
        auto init_ret = vm.executeChunk(chunk, options.print_init_result);
        if (!init_ret.has_value()) {
            niki::diagnostic::DiagnosticBag diagnostics;
            diagnostics.error(niki::diagnostic::events::LauncherCode::InitRuntimeError, "Initialization failed.");
            return std::unexpected(std::move(diagnostics));
        }
    }

    // 2) 查找入口函数对象（由 linker 产出的 entry_name_id 决定）。
    // 注意：这里使用的是“已决议 ID”，不是运行期按字符串再解析，避免名字二义性和额外查找开销。
    vm::ObjFunction *entry = vm.lookupGlobalFunctionById(program.entry_name_id);
    if (entry == nullptr) {
        niki::diagnostic::DiagnosticBag diagnostics;
        diagnostics.error(niki::diagnostic::events::LauncherCode::EntryLookupFailed, "Entry function not found.");
        return std::unexpected(std::move(diagnostics));
    }

    // 3) 执行入口函数并返回其结果。
    // 入口返回不代表 CPU 停止；只是 VM 将控制权与结果值交还给 launcher/宿主。
    auto entry_ret = vm.executeFunction(entry, options.print_entry_result);
    if (!entry_ret.has_value()) {
        niki::diagnostic::DiagnosticBag diagnostics;
        diagnostics.error(niki::diagnostic::events::LauncherCode::EntryRuntimeError, "Entry function execution failed.");
        return std::unexpected(std::move(diagnostics));
    }

    return entry_ret.value();
}

} // namespace niki::runtime