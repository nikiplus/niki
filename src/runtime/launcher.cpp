#include "niki/diagnostic/codes.hpp"
#include "niki/runtime/launcher.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/value.hpp"
#include <expected>

namespace niki::runtime {

std::expected<vm::Value, niki::diagnostic::DiagnosticBag> Launcher::launchProgram(vm::VM &vm,
                                                                                   const linker::LinkedProgram &program,
                                                                                   const LaunchOptions options) {
    // 启动流程分两段：
    // A. 执行所有模块初始化代码
    // B. 可选调用入口函数并返回结果

    // 1) 执行链接产物中的初始化段（按 linker 给出的顺序）。
    // last_init 保存“最后一个 init chunk”的返回值，供无入口模式返回。
    vm::Value last_init = vm::Value::makeNil();
    for (const auto &chunk : program.init_chunks) {
        auto init_ret = vm.executeChunk(chunk, options.print_init_result);
        if (!init_ret.has_value()) {
            niki::diagnostic::DiagnosticBag diagnostics;
            diagnostics.reportError(niki::diagnostic::DiagnosticStage::Launcher,
                                    niki::diagnostic::codes::launcher::InitRuntimeError, "Initialization failed.");
            return std::unexpected(std::move(diagnostics));
        }
        last_init = init_ret.value();
    }

    // 2) 若配置为“仅初始化模式”，直接返回初始化阶段结果。
    if (!options.call_entry) {
        return last_init;
    }

    // 3) 查找入口函数对象（由 linker 产出的 entry_name_id 决定）。
    vm::ObjFunction *entry = vm.lookupGlobalFunctionById(program.entry_name_id);
    if (entry == nullptr) {
        niki::diagnostic::DiagnosticBag diagnostics;
        diagnostics.reportError(niki::diagnostic::DiagnosticStage::Launcher,
                                niki::diagnostic::codes::launcher::EntryLookupFailed, "Entry function not found.");
        return std::unexpected(std::move(diagnostics));
    }

    // 4) 执行入口函数并返回其结果。
    auto entry_ret = vm.executeFunction(entry, options.print_entry_result);
    if (!entry_ret.has_value()) {
        niki::diagnostic::DiagnosticBag diagnostics;
        diagnostics.reportError(niki::diagnostic::DiagnosticStage::Launcher,
                                niki::diagnostic::codes::launcher::EntryRuntimeError, "Entry function execution failed.");
        return std::unexpected(std::move(diagnostics));
    }

    return entry_ret.value();
}

} // namespace niki::runtime