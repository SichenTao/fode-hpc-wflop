cmake_minimum_required(VERSION 3.20)
project(geoga_anholt_structured VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(geoga_anholt_core
    ../fode_cpp/src/executor.cpp
    src/problem.cpp
    src/evolution.cpp
)
target_include_directories(geoga_anholt_core PUBLIC
    include
    ../fode_cpp/include
)
target_link_libraries(geoga_anholt_core PUBLIC Threads::Threads)
target_compile_options(geoga_anholt_core PRIVATE
    -O3
    -ffp-contract=off
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)

add_executable(geoga_anholt_hpc src/main.cpp)
target_link_libraries(geoga_anholt_hpc PRIVATE geoga_anholt_core)
target_compile_options(geoga_anholt_hpc PRIVATE
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
    add_executable(geoga_anholt_problem_test tests/problem_test.cpp)
    target_link_libraries(
        geoga_anholt_problem_test PRIVATE geoga_anholt_core
    )
    target_compile_options(geoga_anholt_problem_test PRIVATE
        -O3 -ffp-contract=off -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
    add_test(
        NAME geoga_anholt_structured_problem
        COMMAND geoga_anholt_problem_test
            ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/geoga_anholt_structured_declared_proxy_case.json
    )

    add_executable(geoga_anholt_evolution_test tests/evolution_test.cpp)
    target_link_libraries(
        geoga_anholt_evolution_test PRIVATE geoga_anholt_core
    )
    target_compile_options(geoga_anholt_evolution_test PRIVATE
        -O3 -ffp-contract=off -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
    add_test(
        NAME geoga_anholt_structured_evolution
        COMMAND geoga_anholt_evolution_test
            ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/geoga_anholt_structured_declared_proxy_case.json
    )
endif()
