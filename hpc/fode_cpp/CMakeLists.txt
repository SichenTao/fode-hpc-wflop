cmake_minimum_required(VERSION 3.20)
project(fode_cpp_hpc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenMP REQUIRED)

add_executable(fode_cpp_hpc
    src/case_loader.cpp
    src/executor.cpp
    src/evaluator.cpp
    src/main.cpp
    src/optimizer.cpp
)

target_include_directories(fode_cpp_hpc PRIVATE include)
target_link_libraries(fode_cpp_hpc PRIVATE OpenMP::OpenMP_CXX)
target_compile_options(fode_cpp_hpc PRIVATE
    -O3
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)

include(CTest)
if(BUILD_TESTING)
    add_test(
        NAME fode_cpp_hpc_self_check
        COMMAND fode_cpp_hpc
            --self-check
            --cases ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/benchmark_cases.json
    )
endif()
