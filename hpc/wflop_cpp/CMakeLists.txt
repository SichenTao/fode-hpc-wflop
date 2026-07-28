cmake_minimum_required(VERSION 3.20)
project(wflop_cpp_hpc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)

add_executable(wflop_cpp_hpc
    ../fode_cpp/src/case_loader.cpp
    ../fode_cpp/src/evaluator.cpp
    ../fode_cpp/src/executor.cpp
    ../fode_cpp/src/optimizer.cpp
    src/registry.cpp
    src/algorithms.cpp
    src/main.cpp
)

target_include_directories(wflop_cpp_hpc PRIVATE
    include
    ../fode_cpp/include
)
target_link_libraries(wflop_cpp_hpc PRIVATE
    OpenMP::OpenMP_CXX
    Threads::Threads
)
target_compile_options(wflop_cpp_hpc PRIVATE
    -O3
    -ffp-contract=off
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)

include(CTest)
if(BUILD_TESTING)
    add_test(
        NAME registry
        COMMAND wflop_cpp_hpc --list-algorithms
    )
    add_test(
        NAME problem_registry
        COMMAND wflop_cpp_hpc --list-problems
    )
    add_test(
        NAME ise_rename_guard
        COMMAND wflop_cpp_hpc
            --check-ise-rename
            --cases ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/benchmark_cases.json
    )
    add_test(
        NAME registered_algorithm_smoke
        COMMAND wflop_cpp_hpc
            --self-check
            --cases ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/benchmark_cases.json
            --models ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/models/sugga_cpp
    )
endif()
