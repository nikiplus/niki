cmake_minimum_required(VERSION 3.20)

# [Architecture Check] Ensure vcpkg toolchain is loaded before project()
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
    else()
        message(FATAL_ERROR 
            "\n[CRITICAL ARCHITECTURE FAILURE]\n"
            "Dependency management system (vcpkg) is missing.\n"
            "Build system cannot proceed without a valid toolchain.\n"
            "Action Required: Set VCPKG_ROOT environment variable to your vcpkg installation path.\n"
        )
    endif()
endif()

project(NIKIROLL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ENGINE_SRC
    src/main.cpp
    src/vm/parser.cpp
    src/vm/scanner.cpp
    src/vm/ast.cpp
    src/vm/ast_printer.cpp
)
add_executable(NIKIROLL ${ENGINE_SRC})

include_directories(./)


find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)

target_link_libraries(NIKIROLL PRIVATE
    fmt::fmt
    spdlog::spdlog
    EnTT::EnTT
)