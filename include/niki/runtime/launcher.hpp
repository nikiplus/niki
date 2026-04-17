#pragma once
#include "niki/linker/linker.hpp"
#include "niki/vm/value.hpp"
#include "niki/vm/vm.hpp"
#include <expected>
#include <string>

namespace niki::runtime {

enum class LaunchErrorCode {
    INIT_RUNTIME_ERROR,
    ENTRY_LOOKUP_FAILED,
    ENTRY_RUNTIME_ERROR,
};

struct LaunchError {
    LaunchErrorCode code;
    std::string message;
};

struct LaunchOptions {
    bool print_init_result = false;
    bool print_entry_result = true;
    bool call_entry = true;
};

class Launcher {
  public:
    std::expected<vm::Value, LaunchError> launchProgram(vm::VM &vm, const linker::LinkedProgram &program,
                                                        const LaunchOptions options);
};
} // namespace niki::runtime