#include "niki/driver/driver.hpp"
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
        for (const auto &driver_error : run_result.error()) {
            std::cerr << "[错误]" << driver_error.message;
            if (!driver_error.file.empty()) {
                std::cerr << " @ " << driver_error.file;
                std::cerr << "\n";
            }
        }
        return 65;
    }
    return 0;
}