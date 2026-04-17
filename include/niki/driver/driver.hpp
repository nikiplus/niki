#pragma once
#include "niki/linker/linker.hpp"
#include "niki/runtime/launcher.hpp"
#include "niki/syntax/global_interner.hpp"
#include "niki/vm/value.hpp"
#include <expected>
#include <string>
#include <vector>

namespace niki::driver {
enum class DriverErrorCode {
    IO_ERROR,
    SCAN_ERROR,
    PARSE_ERROR,
    TYPE_ERROR,
    COMPILE_ERROR,
    LINK_ERROR,
    LAUNCH_ERROR
};
struct DriverError {
    DriverErrorCode code;
    std::string message;
    std::string file;
};
struct DriverOptions {
    bool recursive_scan = true;
    std::string file_ext = ".nk";
    std::string entry_name = "main";
};

class Driver {
  public:
    std::expected<vm::Value, std::vector<DriverError>> runProject(const std::string root_dir,
                                                                  const DriverOptions &options);

  private:
    std::vector<std::string> collectNkFiles(const std::string &root_dir, const DriverOptions &options);

    std::expected<linker::CompileModule, DriverError> compileOneModule(const std::string &source_path,
                                                                       syntax::GlobalInterner &interner);

    std::expected<std::vector<linker::CompileModule>, std::vector<DriverError>> compileAll(
        const std::vector<std::string> &files);
};
} // namespace niki::driver