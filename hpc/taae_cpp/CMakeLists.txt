cmake_minimum_required(VERSION 3.20)
project(taae_cpp_hpc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(taae_cpp_core
    src/model.cpp
)
target_include_directories(taae_cpp_core PUBLIC
    include
)
target_compile_options(taae_cpp_core PRIVATE
    -O3
    -ffp-contract=off
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)

add_library(taae_evolution_core
    ../fode_cpp/src/case_loader.cpp
    ../fode_cpp/src/executor.cpp
    ../wflop_cpp/src/problems/taae_zhangbei_structured_proxy.cpp
    src/evolution.cpp
)
target_include_directories(taae_evolution_core PUBLIC
    include
    ../fode_cpp/include
    ../wflop_cpp/include
)
target_link_libraries(taae_evolution_core PUBLIC
    taae_cpp_core
    Threads::Threads
)
target_compile_options(taae_evolution_core PRIVATE
    -O3
    -ffp-contract=off
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)

add_executable(taae_evolution_hpc src/main.cpp)
target_link_libraries(taae_evolution_hpc PRIVATE taae_evolution_core)
target_compile_options(taae_evolution_hpc PRIVATE
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
    add_executable(taae_model_test tests/model_test.cpp)
    target_link_libraries(taae_model_test PRIVATE taae_cpp_core)
    target_compile_options(taae_model_test PRIVATE
        -O3
        -ffp-contract=off
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    add_test(
        NAME taae_transformer_model
        COMMAND taae_model_test
    )

    add_executable(taae_evolution_test tests/evolution_test.cpp)
    target_link_libraries(taae_evolution_test PRIVATE taae_evolution_core)
    target_compile_options(taae_evolution_test PRIVATE
        -O3
        -ffp-contract=off
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    add_test(
        NAME taae_transformer_evolution
        COMMAND taae_evolution_test
            ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/taae_zhangbei_structured_declared_proxy_cases.json
    )
endif()
