cmake_minimum_required(VERSION 3.20)

find_package(Threads REQUIRED)

add_executable(pbea_cpp_hpc
    ../fode_cpp/src/executor.cpp
    src/main.cpp
)

target_include_directories(pbea_cpp_hpc PRIVATE
    ../fode_cpp/include
)
target_link_libraries(pbea_cpp_hpc PRIVATE Threads::Threads)
target_compile_features(pbea_cpp_hpc PRIVATE cxx_std_20)
target_compile_options(pbea_cpp_hpc PRIVATE
    -O3
    -ffp-contract=off
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)

if(BUILD_TESTING)
    add_test(NAME pbea_cpp_help COMMAND pbea_cpp_hpc --help)
endif()
