cmake_minimum_required(VERSION 3.20)
project(bde_ws56_declared_proxy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(bde_ws56_core
    ../fode_cpp/src/case_loader.cpp
    ../fode_cpp/src/executor.cpp
    ../fode_cpp/src/evaluator.cpp
    src/evolution.cpp
)
target_include_directories(bde_ws56_core PUBLIC
    include
    ../fode_cpp/include
)
target_link_libraries(bde_ws56_core PUBLIC Threads::Threads)
target_compile_options(bde_ws56_core PRIVATE
    -O3
    -ffp-contract=off
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)

add_executable(bde_ws56_hpc src/main.cpp)
target_link_libraries(bde_ws56_hpc PRIVATE bde_ws56_core)
target_compile_options(bde_ws56_hpc PRIVATE
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
    add_executable(bde_ws56_evolution_test tests/evolution_test.cpp)
    target_link_libraries(bde_ws56_evolution_test PRIVATE bde_ws56_core)
    target_compile_options(bde_ws56_evolution_test PRIVATE
        -O3
        -ffp-contract=off
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    add_test(
        NAME bde_ws56_declared_proxy_evolution
        COMMAND bde_ws56_evolution_test
            ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/bde_ws56_declared_proxy_cases.json
    )
endif()
