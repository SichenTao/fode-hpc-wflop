cmake_minimum_required(VERSION 3.20)

add_library(core99_t28_core src/nguyen_t28.cpp)
target_include_directories(core99_t28_core PUBLIC include)
target_link_libraries(core99_t28_core PUBLIC ${TORCH_LIBRARIES})
target_compile_features(core99_t28_core PUBLIC cxx_std_20)
target_compile_options(
    core99_t28_core PRIVATE -O3 -march=native -ffp-contract=off
    -Wall -Wextra -Wpedantic
)

add_executable(core99_t28_hpc src/nguyen_t28_main.cpp)
target_link_libraries(core99_t28_hpc PRIVATE core99_t28_core)

if(BUILD_TESTING)
    add_executable(core99_t28_test tests/nguyen_t28_test.cpp)
    target_link_libraries(core99_t28_test PRIVATE core99_t28_core)
    add_test(
        NAME core99_t28_cpp_libtorch
        COMMAND core99_t28_test
            ${CMAKE_SOURCE_DIR}/shared/data/core99_t28
    )
    set_tests_properties(core99_t28_cpp_libtorch PROPERTIES TIMEOUT 300)
endif()
