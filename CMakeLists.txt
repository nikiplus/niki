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
    src/l0_core/ir/module_ir.cpp
    src/l0_core/ir/verify.cpp
    src/l0_core/ir/builder.cpp
    src/l0_core/ir/builder_expression.cpp
    src/l0_core/ir/builder_statement.cpp
    src/l0_core/ir/builder_declaration.cpp
    src/l0_core/ir/extensions.cpp
    src/l0_core/ir/lower_to_chunk.cpp
    src/l0_core/syntax/string_interner.cpp
    src/l0_core/syntax/scanner.cpp
    src/l0_core/syntax/parse.cpp
    src/l0_core/syntax/parser_declaration.cpp
    src/l0_core/syntax/parser_expression.cpp
    src/l0_core/syntax/parser_statement.cpp
    src/l0_core/syntax/ast.cpp
    src/l0_core/semantic/type_checker.cpp
    src/l0_core/semantic/type_checker_expr.cpp
    src/l0_core/semantic/type_checker_stmt.cpp
    src/l0_core/semantic/type_checker_decl.cpp
    src/l0_core/semantic/extensions.cpp
    src/l0_core/semantic/type_checker_pre_decl.cpp
    src/l0_core/semantic/type_arena.cpp
    src/l0_core/semantic/module_id.cpp
    src/l0_core/semantic/module_namespace.cpp
    src/l0_core/linker/linker_facade.cpp
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
    src/meta/precompile/parse_stage.cpp
    src/meta/precompile/predeclare_stage.cpp
    src/meta/precompile/module_context_stage.cpp
    src/meta/orchestrator/compiler_orchestrator.cpp
    src/meta/orchestrator/compile_pipeline.cpp
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
    test/syntax/parser_stmt_test.cpp
    test/syntax/parser_expr_matrix_test.cpp
    test/syntax/parser_stmt_matrix_test.cpp
    test/syntax/parser_decl_independent_test.cpp
    test/syntax/parser_decl_dependent_test.cpp
    test/semantic/type_checker_test.cpp
    test/semantic/type_checker_stmt_test.cpp
    test/semantic/module_chain_test.cpp
    test/ir/builder_test.cpp
    test/ir/verify_test.cpp
    test/ir/module_ir_test.cpp
    test/ir/lower_to_chunk_test.cpp
    test/driver/driver_project_test.cpp
    test/linker/linker_test.cpp
    test/runtime/launcher_test.cpp
    test/runtime/endpoint_test.cpp
    test/l1_domain/domain_split_test.cpp
    test/diagnostic/diagnostic_test.cpp
    test/helpers/test_helpers.hpp
)

# 注意 GTest::gtest_main 的大小写
target_link_libraries(niki_tests PRIVATE
    niki_core
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(niki_tests)

find_package(Python3 COMPONENTS Interpreter)

set(NIKI_CASES_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/scripts/cases)
set(NIKI_FAIL_EXIT 65)

if(Python3_Interpreter_FOUND)
    add_custom_target(check_layer_boundaries
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_layer_boundaries.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Check physical layer boundary includes"
    )

    add_test(NAME layer_boundaries
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_layer_boundaries.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_test(NAME smoke_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_case.py
                $<TARGET_FILE:NIKI> ${CMAKE_CURRENT_SOURCE_DIR}/scripts/smoke 0
    )

    set(NIKI_CASE_SUCCESS
        success/01_multi_file_basic
        success/02_init_order
        success/03_multi_decl_stable
        success/04_dice_basic
        success/06_comment_tokens
    )
    foreach(case_rel IN LISTS NIKI_CASE_SUCCESS)
        string(REPLACE "/" "_" case_id ${case_rel})
        add_test(NAME cases_${case_id}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_case.py
                    $<TARGET_FILE:NIKI> ${NIKI_CASES_ROOT}/${case_rel} 0
        )
    endforeach()

    set(NIKI_CASE_FAIL
        fail/link_01_no_entry
        fail/link_02_multiple_entry
        fail/link_03_id_conflict
        fail/semantic_kits_duplicate_alias
        fail/semantic_01_cross_file_call
        fail/runtime_01_init_error
        fail/runtime_02_entry_error
    )
    foreach(case_rel IN LISTS NIKI_CASE_FAIL)
        string(REPLACE "/" "_" case_id ${case_rel})
        add_test(NAME cases_${case_id}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_case.py
                    $<TARGET_FILE:NIKI> ${NIKI_CASES_ROOT}/${case_rel} ${NIKI_FAIL_EXIT}
        )
    endforeach()
endif()
