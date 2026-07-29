cmake_minimum_required(VERSION 3.20)
project(wflop_cpp_hpc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)

if(NOT TARGET fode_core)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp
        ${CMAKE_CURRENT_BINARY_DIR}/fode_cpp_core
    )
endif()

add_executable(wflop_cpp_hpc
    src/registry.cpp
    src/algorithms.cpp
    src/algorithms/alga_attention_declared_reconstruction.cpp
    src/algorithms/ppo.cpp
    src/algorithms/rlfode_seeded_reconstruction.cpp
    src/algorithms/rlpso.cpp
    src/problems/taae_zhangbei_structured_proxy.cpp
    src/main.cpp
)

add_executable(rlfode_train_qtable
    src/registry.cpp
    src/algorithms/rlfode_seeded_reconstruction.cpp
    src/rlfode_train_qtable.cpp
)

foreach(target wflop_cpp_hpc rlfode_train_qtable)
target_include_directories(${target} PRIVATE
    include
    ../fode_cpp/include
)
target_link_libraries(${target} PRIVATE
    fode_core
    OpenMP::OpenMP_CXX
    Threads::Threads
)
target_compile_options(${target} PRIVATE
    -O3
    -ffp-contract=off
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(rlpso_transition_test tests/rlpso_transition_test.cpp)
    target_include_directories(rlpso_transition_test PRIVATE include)
    add_test(NAME rlpso_transition_fixture COMMAND rlpso_transition_test)
    add_executable(rlfode_math_test
        src/registry.cpp
        src/algorithms/rlfode_seeded_reconstruction.cpp
        tests/rlfode_math_test.cpp
    )
    target_include_directories(rlfode_math_test PRIVATE
        include
        ../fode_cpp/include
    )
    target_link_libraries(rlfode_math_test PRIVATE
        fode_core
        OpenMP::OpenMP_CXX
        Threads::Threads
    )
    add_test(
        NAME rlfode_math_fixture
        COMMAND rlfode_math_test
            ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/benchmark_cases.json
    )
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
            --rlfode-models ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/models/fqfode_seeded
    )
    add_test(
        NAME fode_frontend_parity
        COMMAND python3
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/test_fode_frontend_parity.py
            --analysis-binary $<TARGET_FILE:fode_cpp_hpc>
            --formal-binary $<TARGET_FILE:wflop_cpp_hpc>
            --cases ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/benchmark_cases.json
    )
    add_test(
        NAME alga_attention_declared_reconstruction
        COMMAND python3
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/test_alga_attention_reconstruction.py
            --binary $<TARGET_FILE:wflop_cpp_hpc>
            --common-cases ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/benchmark_cases.json
            --transfer-cases ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/alga_guishan_planar_transfer_cases.json
    )
    add_executable(taae_problem_test
        src/problems/taae_zhangbei_structured_proxy.cpp
        tests/taae_problem_test.cpp
    )
    target_include_directories(taae_problem_test PRIVATE
        include
        ../fode_cpp/include
    )
    target_link_libraries(taae_problem_test PRIVATE
        fode_core
        OpenMP::OpenMP_CXX
        Threads::Threads
    )
    target_compile_options(taae_problem_test PRIVATE
        -O3
        -ffp-contract=off
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    add_test(
        NAME taae_problem_fixture
        COMMAND taae_problem_test
            ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/taae_zhangbei_structured_declared_proxy_cases.json
    )
endif()
