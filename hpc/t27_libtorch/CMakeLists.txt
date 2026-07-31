cmake_minimum_required(VERSION 3.20)

add_library(core99_t27_core src/shin_t27.cpp)
target_include_directories(
    core99_t27_core
    PUBLIC include
)
target_link_libraries(core99_t27_core PUBLIC ${TORCH_LIBRARIES})
target_compile_features(core99_t27_core PUBLIC cxx_std_20)
target_compile_options(
    core99_t27_core
    PRIVATE
        -O3
        -march=native
        -ffp-contract=off
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(core99_t27_hpc src/shin_t27_main.cpp)
target_link_libraries(core99_t27_hpc PRIVATE core99_t27_core)

if(BUILD_TESTING)
    add_executable(core99_t27_test tests/shin_t27_test.cpp)
    target_link_libraries(core99_t27_test PRIVATE core99_t27_core)
    add_test(NAME core99_t27_cpp_libtorch COMMAND core99_t27_test)
    set_tests_properties(core99_t27_cpp_libtorch PROPERTIES TIMEOUT 300)
endif()
