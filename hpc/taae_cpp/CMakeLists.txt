cmake_minimum_required(VERSION 3.20)
project(taae_cpp_hpc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
endif()
