cmake_minimum_required(VERSION 3.20)
project(NIKIROLL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

add_library(niki_core STATIC
    src/syntax/scanner.cpp
    src/syntax/parse.cpp
    src/syntax/parser_declaration.cpp
    src/syntax/parser_expression.cpp
    src/syntax/parser_statement.cpp
    src/syntax/ast.cpp
    src/syntax/compiler.cpp
    src/syntax/compiler_expression.cpp
    src/syntax/compiler_statement.cpp
    src/syntax/compiler_declaration.cpp
    src/semantic/type_checker.cpp
    src/semantic/type_checker_expr.cpp
    src/semantic/type_checker_stmt.cpp
    src/semantic/type_checker_decl.cpp
    src/vm/vm.cpp
    src/debug/logger.cpp
)

target_include_directories(niki_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(niki_core PUBLIC
    fmt::fmt
    spdlog::spdlog
    EnTT::EnTT
)
target_compile_definitions(niki_core PUBLIC
  $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
  $<$<NOT:$<CONFIG:Debug>>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
)

add_executable(NIKI src/main.cpp)
target_link_libraries(NIKI PRIVATE niki_core)

# 开启 CTest 测试支持 (极其重要)
enable_testing()

# 注意这里的路径是 test/，不是 tests/
add_executable(niki_tests 
    test/syntax/parser_test.cpp
    test/syntax/compiler_test.cpp
)

# 注意 GTest::gtest_main 的大小写
target_link_libraries(niki_tests PRIVATE
    niki_core
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(niki_tests)
