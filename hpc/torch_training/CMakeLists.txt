cmake_minimum_required(VERSION 3.20)

add_executable(
    wflop_libtorch_training_hpc
    src/main.cpp
)
target_link_libraries(
    wflop_libtorch_training_hpc
    PRIVATE ${TORCH_LIBRARIES}
)
target_compile_features(wflop_libtorch_training_hpc PRIVATE cxx_std_20)
target_compile_options(
    wflop_libtorch_training_hpc
    PRIVATE
        -O2
        -ffp-contract=off
        -Wall
        -Wextra
        -Wpedantic
)
