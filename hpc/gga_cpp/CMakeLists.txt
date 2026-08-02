cmake_minimum_required(VERSION 3.20)

find_package(Threads REQUIRED)

add_executable(gga_cpp_hpc
    ../fode_cpp/src/executor.cpp
    src/main.cpp
)

target_include_directories(gga_cpp_hpc PRIVATE
    ../fode_cpp/include
)
target_link_libraries(gga_cpp_hpc PRIVATE Threads::Threads)
target_compile_features(gga_cpp_hpc PRIVATE cxx_std_20)
target_compile_options(gga_cpp_hpc PRIVATE
    -O3
    -ffp-contract=off
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)

if(BUILD_TESTING)
    add_executable(tmoea_topology_eq16_test
        ../fode_cpp/src/executor.cpp
        tests/tmoea_topology_test.cpp
    )
    target_include_directories(tmoea_topology_eq16_test PRIVATE
        ../fode_cpp/include
    )
    target_link_libraries(tmoea_topology_eq16_test PRIVATE Threads::Threads)
    target_compile_features(tmoea_topology_eq16_test PRIVATE cxx_std_20)
    target_compile_options(tmoea_topology_eq16_test PRIVATE
        -O2
        -ffp-contract=off
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    add_test(
        NAME tmoea_topology_eq16_fixture
        COMMAND tmoea_topology_eq16_test
    )
    add_test(
        NAME gga_cpp_help
        COMMAND gga_cpp_hpc --help
    )
endif()
