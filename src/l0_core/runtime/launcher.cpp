#include "niki/l0_core/runtime/launcher.hpp"
#include "niki/meta/runtime_host/runtime_host.hpp"


namespace niki::runtime {

std::expected<vm::Value, niki::diagnostic::DiagnosticBag> Launcher::launchProgram(vm::VM &vm,
                                                                                    const linker::LinkedProgram &program,
                                                                                    const LaunchOptions options) {
    meta::runtime_host::RuntimeHost host;
    meta::runtime_host::HostLaunchOptions host_options;
    host_options.print_init_result = options.print_init_result;
    host_options.print_entry_result = options.print_entry_result;
    return host.launch(vm, program, host_options);
}

} // namespace niki::runtime