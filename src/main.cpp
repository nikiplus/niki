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

    auto ret = driver.runProject(argv[1], options);

    if (!ret.has_value()) {
        for (const auto &err : ret.error()) {
            std::cerr << "[错误]" << err.message;
            if (!err.file.empty()) {
                std::cerr << " @ " << err.file;
                std::cerr << "\n";
            }
        }
        return 65;
    }
    return 0;
}