#include "niki/driver/driver.hpp"
#include "niki/l0_core/diagnostic/renderer.hpp"
#include <iostream>
#include <string_view>

// argc和argv是c++和操作系统交互返回的标准值——后续return的各类值也是一样，属系统错误返回标准值
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
            return 64; // 参数个数错误，活第三个参数不是支持的 --diagnosic-format = json/test
        }
    }

    // 实际驱动阶段
    // 加载驱动器及配置
    niki::driver::Driver driver;
    niki::driver::DriverOptions options;
    // 更改配置值
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
        return 65; // run result无值
    }
    return 0; // 程序正常结束
}