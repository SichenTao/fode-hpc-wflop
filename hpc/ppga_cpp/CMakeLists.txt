cmake_minimum_required(VERSION 3.20)
project(ppga_nantong_structured_3d VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(ppga_nantong_core
    ../fode_cpp/src/executor.cpp
    src/problem.cpp
    src/evolution.cpp
)
target_include_directories(ppga_nantong_core PUBLIC
    include
    ../fode_cpp/include
)
target_link_libraries(ppga_nantong_core PUBLIC Threads::Threads)
target_compile_options(ppga_nantong_core PRIVATE
    -O3
    -ffp-contract=off
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)

add_executable(ppga_nantong_hpc src/main.cpp)
target_link_libraries(ppga_nantong_hpc PRIVATE ppga_nantong_core)
target_compile_options(ppga_nantong_hpc PRIVATE
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
    add_executable(
        ppga_nantong_problem_test
        tests/problem_test.cpp
    )
    target_link_libraries(
        ppga_nantong_problem_test
        PRIVATE ppga_nantong_core
    )
    target_compile_options(ppga_nantong_problem_test PRIVATE
        -O3
        -ffp-contract=off
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    add_test(
        NAME ppga_nantong_problem
        COMMAND ppga_nantong_problem_test
            ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/ppga_nantong_structured_3d_declared_proxy_cases.json
    )

    add_executable(
        ppga_nantong_evolution_test
        tests/evolution_test.cpp
    )
    target_link_libraries(
        ppga_nantong_evolution_test
        PRIVATE ppga_nantong_core
    )
    target_compile_options(ppga_nantong_evolution_test PRIVATE
        -O3
        -ffp-contract=off
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    add_test(
        NAME ppga_nantong_evolution
        COMMAND ppga_nantong_evolution_test
            ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/ppga_nantong_structured_3d_declared_proxy_cases.json
    )
endif()
