cmake_minimum_required(VERSION 3.20)
project(NIKIROLL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NIKI_SRC
    src/main.cpp
)
add_executable(NIKI ${NIKI_SRC})

include_directories(./)


find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)

target_link_libraries(NIKI PRIVATE
    fmt::fmt
    spdlog::spdlog
    EnTT::EnTT
)