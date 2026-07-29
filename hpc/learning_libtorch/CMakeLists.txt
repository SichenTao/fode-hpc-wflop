cmake_minimum_required(VERSION 3.20)

add_library(
    plan004_learning_libtorch
    src/models.cpp
)
target_include_directories(
    plan004_learning_libtorch
    PUBLIC include
)
target_link_libraries(
    plan004_learning_libtorch
    PUBLIC ${TORCH_LIBRARIES}
)
target_compile_features(plan004_learning_libtorch PUBLIC cxx_std_20)
target_compile_options(
    plan004_learning_libtorch
    PRIVATE
        -O2
        -ffp-contract=off
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(
    plan004_learning_target_hpc
    src/main.cpp
)
target_link_libraries(
    plan004_learning_target_hpc
    PRIVATE plan004_learning_libtorch
)
target_compile_options(
    plan004_learning_target_hpc
    PRIVATE
        -O2
        -ffp-contract=off
        -Wall
        -Wextra
        -Wpedantic
)

if(BUILD_TESTING)
    add_executable(
        plan004_learning_models_test
        tests/models_test.cpp
    )
    target_link_libraries(
        plan004_learning_models_test
        PRIVATE plan004_learning_libtorch
    )
    target_compile_options(
        plan004_learning_models_test
        PRIVATE
            -O1
            -ffp-contract=off
            -Wall
            -Wextra
            -Wpedantic
    )
    add_test(
        NAME taae_transformer_libtorch_cpu
        COMMAND plan004_learning_models_test --method taae --backend cpu
    )
    add_test(
        NAME alga_attention_libtorch_cpu
        COMMAND plan004_learning_models_test --method alga --backend cpu
    )
    add_test(
        NAME rlpso_ppo_libtorch_cpu
        COMMAND plan004_learning_models_test --method rlpso --backend cpu
    )
    add_test(
        NAME artifact_optimization_bridge_cpu
        COMMAND plan004_learning_models_test --method all --backend cpu
    )
endif()
