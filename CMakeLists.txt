cmake_minimum_required(VERSION 3.20)
project(NIKIROLL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

add_library(niki_core STATIC
    src/scanner.cpp
)

target_include_directories(niki_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(niki_core PUBLIC
    fmt::fmt
    spdlog::spdlog
    EnTT::EnTT
)
add_executable(NIKI src/main.cpp)
target_link_libraries(NIKI PRIVATE niki_core)

# 开启 CTest 测试支持 (极其重要)
enable_testing()

# 注意这里的路径是 test/，不是 tests/
add_executable(niki_tests test/scanner_test.cpp)

# 注意 GTest::gtest_main 的大小写
target_link_libraries(niki_tests PRIVATE
    niki_core
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(niki_tests)
