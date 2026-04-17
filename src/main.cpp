#include "niki/driver/driver.hpp"
#include "niki/diagnostic/renderer.hpp"
#include <iostream>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "用法:niki<project_root>\n";
        return 64;
    }

    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = true;
    options.file_ext = ".nk";
    options.entry_name = "main";

    auto run_result = driver.runProject(argv[1], options);

    if (!run_result.has_value()) {
        std::cerr << niki::diagnostic::renderDiagnosticBagText(run_result.error()) << "\n";
        return 65;
    }
    return 0;
}