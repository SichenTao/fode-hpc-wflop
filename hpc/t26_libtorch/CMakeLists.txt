cmake_minimum_required(VERSION 3.20)

add_library(core99_t26_core src/li_t26.cpp)
target_include_directories(core99_t26_core PUBLIC include)
target_link_libraries(core99_t26_core PUBLIC ${TORCH_LIBRARIES})
target_compile_features(core99_t26_core PUBLIC cxx_std_20)
target_compile_options(
    core99_t26_core PRIVATE -O3 -march=native -ffp-contract=off
    -Wall -Wextra -Wpedantic
)

add_executable(core99_t26_hpc src/li_t26_main.cpp)
target_link_libraries(core99_t26_hpc PRIVATE core99_t26_core)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(core99_t26_test tests/li_t26_test.cpp)
    target_link_libraries(core99_t26_test PRIVATE core99_t26_core)
    add_test(NAME core99_t26_cpp_libtorch COMMAND core99_t26_test)
    set_tests_properties(core99_t26_cpp_libtorch PROPERTIES TIMEOUT 600)
    add_test(
        NAME core99_t26_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/validate_core99_t26.py
            --binary $<TARGET_FILE:core99_t26_hpc>
    )
    set_tests_properties(core99_t26_h5 PROPERTIES TIMEOUT 900)
endif()
