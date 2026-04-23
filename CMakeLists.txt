cmake_minimum_required(VERSION 3.20)
project(NIKIROLL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

set(NIKI_L0_CORE_SOURCES
    src/l0_core/diagnostic/diagnostic.cpp
    src/l0_core/diagnostic/renderer.cpp
    src/l0_core/syntax/global_interner.cpp
    src/l0_core/syntax/scanner.cpp
    src/l0_core/syntax/parse.cpp
    src/l0_core/syntax/parser_declaration.cpp
    src/l0_core/syntax/parser_expression.cpp
    src/l0_core/syntax/parser_statement.cpp
    src/l0_core/syntax/ast.cpp
    src/l0_core/syntax/compiler.cpp
    src/l0_core/syntax/compiler_expression.cpp
    src/l0_core/syntax/compiler_statement.cpp
    src/l0_core/syntax/compiler_declaration.cpp
    src/l0_core/syntax/compiler_pre_decl.cpp
    src/l0_core/semantic/type_checker.cpp
    src/l0_core/semantic/type_checker_expr.cpp
    src/l0_core/semantic/type_checker_stmt.cpp
    src/l0_core/semantic/type_checker_decl.cpp
    src/l0_core/semantic/type_checker_pre_decl.cpp
    src/l0_core/semantic/global_symbol_table.cpp
    src/l0_core/semantic/global_type_arena.cpp
    src/l0_core/linker/linker.cpp
    src/l0_core/runtime/launcher.cpp
    src/l0_core/vm/vm.cpp
)

set(NIKI_L1_DOMAIN_SOURCES
    src/l1_domain/ir.cpp
    src/l1_domain/analyzer.cpp
    src/l1_domain/validator.cpp
)

set(NIKI_L2_SCHEDULER_SOURCES
    src/l2_scheduler/temporal_scheduler.cpp
    src/l2_scheduler/temporal_types.cpp
)

set(NIKI_L3_PLANNER_SOURCES
    src/l3_planner/spatial_plan.cpp
    src/l3_planner/binding_table.cpp
)

set(NIKI_L4_TOPOLOGY_SOURCES
    src/l4_topology/target_query.cpp
    src/l4_topology/kits_window.cpp
)

set(NIKI_L5_REPLAY_SOURCES
    src/l5_replay/trace_event.cpp
    src/l5_replay/trace_writer.cpp
    src/l5_replay/state_hash.cpp
)

set(NIKI_APP_SOURCES
    src/driver/driver.cpp
    src/debug/logger.cpp
    src/debug/diagnostic_logger.cpp
)

add_library(niki_core STATIC
    ${NIKI_L0_CORE_SOURCES}
    ${NIKI_L1_DOMAIN_SOURCES}
    ${NIKI_L2_SCHEDULER_SOURCES}
    ${NIKI_L3_PLANNER_SOURCES}
    ${NIKI_L4_TOPOLOGY_SOURCES}
    ${NIKI_L5_REPLAY_SOURCES}
    ${NIKI_APP_SOURCES}
)

target_include_directories(niki_core PUBLIC
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
    test/syntax/scanner_test.cpp
    test/syntax/parser_test.cpp
    test/syntax/compiler_test.cpp
    test/driver/driver_project_test.cpp
    test/linker/linker_test.cpp
    test/runtime/launcher_test.cpp
)

# 注意 GTest::gtest_main 的大小写
target_link_libraries(niki_tests PRIVATE
    niki_core
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(niki_tests)
