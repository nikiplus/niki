#include "niki/driver/driver.hpp"
#include "niki/diagnostic/renderer.hpp"
#include <iostream>
#include <string_view>

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: niki <project_root> [--diagnostic-format=text|json]\n";
        return 64;
    }

    std::string_view diagnostic_format = "text";
    if (argc == 3) {
        std::string_view flag = argv[2];
        constexpr std::string_view text_flag = "--diagnostic-format=text";
        constexpr std::string_view json_flag = "--diagnostic-format=json";
        if (flag == text_flag) {
            diagnostic_format = "text";
        } else if (flag == json_flag) {
            diagnostic_format = "json";
        } else {
            std::cerr << "Unsupported argument: " << flag << "\n";
            std::cerr << "Usage: niki <project_root> [--diagnostic-format=text|json]\n";
            return 64;
        }
    }

    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    options.recursive_scan = true;
    options.file_ext = ".nk";
    options.entry_name = "main";

    auto run_result = driver.runProject(argv[1], options);

    if (!run_result.has_value()) {
        if (diagnostic_format == "json") {
            std::cerr << niki::diagnostic::renderDiagnosticBagJson(run_result.error()) << "\n";
        } else {
            std::cerr << niki::diagnostic::renderDiagnosticBagText(run_result.error()) << "\n";
        }
        return 65;
    }
    return 0;
}