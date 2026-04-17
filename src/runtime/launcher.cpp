#include "niki/runtime/launcher.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>
#include <expected>

namespace niki::runtime {

std::expected<vm::Value, LaunchError> Launcher::launchProgram(vm::VM &vm, const linker::LinkedProgram &program,
                                                              const LaunchOptions options) {
    // 1)执行合并后的初始化段
    auto init_ret = vm.executeChunk(program.merged_init_chunk, options.print_init_result);
    if (!init_ret.has_value()) {
        return std::unexpected(LaunchError{LaunchErrorCode::INIT_RUNTIME_ERROR, "初始化阶段运行失败"});
    }

    // 2)需要入口就查入口
    if (!options.call_entry) {
        return init_ret.value();
    }

    vm::ObjFunction *entry = vm.lookupGlobalFunctionById(program.entry_name_id);
    if (entry == nullptr) {
        return std::unexpected(LaunchError{LaunchErrorCode::ENTRY_LOOKUP_FAILED, "未找到入口函数main"});
    }

    // 3)执行入口
    auto entry_ret = vm.executeFunction(entry, options.print_entry_result);
    if (!entry_ret.has_value()) {
        return std::unexpected(LaunchError{LaunchErrorCode::ENTRY_RUNTIME_ERROR, "入口函数运行失败"});
    }

    return entry_ret.value();
}

} // namespace niki::runtime