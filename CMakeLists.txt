cmake_minimum_required(VERSION 3.20)
project(fode_hpc_wflop VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(
    WFLOP_ENABLE_TORCH
    "Build Plan-004 LibTorch learning targets and the non-target backend compatibility probe"
    OFF
)
if(WFLOP_ENABLE_TORCH)
    find_package(Torch CONFIG REQUIRED)
    add_subdirectory(hpc/learning_libtorch)
    add_subdirectory(hpc/t26_libtorch)
    add_subdirectory(hpc/t27_libtorch)
    add_subdirectory(hpc/t28_libtorch)
endif()
add_subdirectory(hpc/fode_cpp)
add_subdirectory(hpc/wflop_cpp)
add_subdirectory(hpc/taae_cpp)
add_subdirectory(hpc/ppga_cpp)
add_subdirectory(hpc/bde_ws56_cpp)
add_subdirectory(hpc/geoga_cpp)
add_subdirectory(hpc/gga_cpp)
add_subdirectory(hpc/pbea_cpp)
add_subdirectory(hpc/core99_cpp)
if(WFLOP_ENABLE_TORCH)
    add_subdirectory(hpc/torch_training)
endif()

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/docs/core99_expansion_registry.tsv)
        add_test(
            NAME core99_scope
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_core99_scope.py
        )
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/evidence/core99/paper-dossiers/index.json)
            add_test(
                NAME core99_paper_dossiers
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_core99_paper_dossiers.py
            )
        endif()
    endif()
    add_test(
        NAME lineage_registry
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_lineage_registry.py
    )
    add_test(
        NAME evidence_authority
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_evidence_authority.py
    )
    add_test(
        NAME execution_backends
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_execution_backends.py
            --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME hpc_core_cpu_implementation_traceability
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_hpc_implementation_traceability.py
            --scope core
            --backend cpu
    )
    add_test(
        NAME hpc_core_cpu_runtime_coverage
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_hpc_core_cpu_runtime_coverage.py
            --build-dir ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME hpc_core_equivalence_current_state
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_hpc_equivalence.py
            --scope core
            --strict
    )
    add_test(
        NAME hpc_learning_h5_historical_reopened_regression
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_hpc_equivalence.py
            --scope core
            --historical-reopened-regression
    )
    add_test(
        NAME hpc_core_h5_validation_receipts
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_hpc_core_h5_validation.py
    )
    add_test(
        NAME learning_architecture_contracts
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_learning_architecture_contracts.py
            --scope core
    )
    add_test(
        NAME learning_pdf_consumption
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_learning_pdf_consumption.py
    )
    if(WFLOP_ENABLE_TORCH)
        add_test(
            NAME core99_t27_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_core99_t27.py
                --binary $<TARGET_FILE:core99_t27_hpc>
                --fixture
                ${CMAKE_CURRENT_SOURCE_DIR}/shared/data/core99_t27/floris411_gch_fixture.json
        )
        add_test(
            NAME core99_t28_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_core99_t28.py
                --binary $<TARGET_FILE:core99_t28_hpc>
                --data ${CMAKE_CURRENT_SOURCE_DIR}/shared/data/core99_t28
        )
        add_test(
            NAME hpc_learning_backend_compatibility
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_hpc_learning_backend_compatibility.py
                --binary $<TARGET_FILE:wflop_libtorch_training_hpc>
        )
        add_test(
            NAME plan004_learning_backend_matrix
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_plan004_learning_backends.py
                --binary $<TARGET_FILE:plan004_learning_models_test>
        )
        add_test(
            NAME plan004_artifact_target_optimization
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_plan004_learning_target_artifacts.py
                --binary $<TARGET_FILE:plan004_learning_target_hpc>
        )
        add_test(
            NAME plan005_learning_phase_topology
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_plan005_learning_phase_topology.py
                --trainer $<TARGET_FILE:plan004_learning_target_hpc>
                --taae-binary $<TARGET_FILE:taae_evolution_hpc>
                --wflop-binary $<TARGET_FILE:wflop_cpp_hpc>
        )
        set_tests_properties(
            plan005_learning_phase_topology
            PROPERTIES TIMEOUT 600
        )
    endif()
    if(CORE99_ENABLE_T30)
        add_test(
            NAME core99_t30_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_core99_t30.py
                --binary $<TARGET_FILE:core99_t30_hpc>
        )
        set_tests_properties(core99_t30_h5 PROPERTIES TIMEOUT 300)
    endif()
    add_test(
        NAME source_dossiers
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_source_dossiers.py
    )
    add_test(
        NAME target_source_fact_declarations
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_target_source_fact_declarations.py
    )
    add_test(
        NAME all_cpp_source_fact_declarations
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_source_fact_declarations.py
    )
    add_test(
        NAME reconstruction_decisions
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_reconstruction_decisions.py
    )
    add_test(
        NAME taae_fact_declarations
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_taae_fact_declarations.py
    )
    add_test(
        NAME taae_semantic_contract
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_taae_semantic_contract.py
    )
    add_test(
        NAME taae_transformer_kernel_contract
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_taae_transformer_kernel.py
    )
    add_test(
        NAME taae_transformer_evolution_contract
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_taae_evolution_contract.py
    )
    add_test(
        NAME ppga_nantong_contract
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_ppga_nantong_contract.py
    )
    add_test(
        NAME geoga_anholt_contract
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_geoga_anholt_contract.py
    )
    add_test(
        NAME geoga_operator_parity
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_geoga_operator_parity.py
    )
    add_test(
        NAME bde_ws56_contract
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_bde_ws56_contract.py
    )
    add_test(
        NAME bde_ws56_transition_parity
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_bde_ws56_transition_parity.py
    )
    add_test(
        NAME tmoea_nysted_r4_contract
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_tmoea_nysted_r4.py
    )
    add_test(
        NAME bde_ws56_independent_oracle
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_bde_ws56_declared_proxy.py
            --binary $<TARGET_FILE:bde_ws56_hpc>
            --cases ${CMAKE_CURRENT_SOURCE_DIR}/shared/contracts/bde_ws56_declared_proxy_cases.json
            --physical-fes 75
            --all-workers 0
    )
    add_test(
        NAME ppo_math
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_ppo_math.py
    )
    add_test(
        NAME lineage_r0_r4_completion
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_lineage_r0_r4.py
    )
    add_test(
        NAME problem_package_registry
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_problem_packages.py
    )
    add_test(
        NAME formal_suite_summary_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/summarize_formal_suite.py
            --self-test
    )
    add_test(
        NAME formal_suite_contracts
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_formal_suite_contracts.py
    )
    set(BDE_SOURCE_REPLAY_CASES
        ${CMAKE_CURRENT_SOURCE_DIR}/.source-cache/generated/bde_source_replay/benchmark_cases.json
    )
    if(EXISTS ${BDE_SOURCE_REPLAY_CASES})
        add_test(
            NAME bde_source_replay
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_bde_source_replay.py
                --binary $<TARGET_FILE:wflop_cpp_hpc>
                --cases ${BDE_SOURCE_REPLAY_CASES}
        )
    endif()
    set(RPSO_SOURCE_PROBLEM_CASES
        ${CMAKE_CURRENT_SOURCE_DIR}/.source-cache/generated/rpso_source_problem/benchmark_cases.json
    )
    if(EXISTS ${RPSO_SOURCE_PROBLEM_CASES})
        add_test(
            NAME rpso_source_problem
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_rpso_source_problem.py
                --binary $<TARGET_FILE:wflop_cpp_hpc>
                --cases ${RPSO_SOURCE_PROBLEM_CASES}
        )
        add_test(
            NAME rlpso_reconstruction
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_rlpso_reconstruction.py
                --binary $<TARGET_FILE:wflop_cpp_hpc>
                --cases ${RPSO_SOURCE_PROBLEM_CASES}
        )
        if(WFLOP_ENABLE_TORCH)
            add_test(
                NAME plan004_learning_full_optimizer_artifacts
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_plan004_learning_full_optimizers.py
                    --trainer $<TARGET_FILE:plan004_learning_target_hpc>
                    --taae-binary $<TARGET_FILE:taae_evolution_hpc>
                    --wflop-binary $<TARGET_FILE:wflop_cpp_hpc>
                    --taae-cases
                    ${CMAKE_CURRENT_SOURCE_DIR}/shared/contracts/taae_zhangbei_structured_declared_proxy_cases.json
                    --alga-cases
                    ${CMAKE_CURRENT_SOURCE_DIR}/shared/contracts/alga_guishan_3d_declared_proxy_cases.json
                    --rlpso-cases ${RPSO_SOURCE_PROBLEM_CASES}
            )
        endif()
    endif()
    set(GGA_GENERATED_ASSETS
        ${CMAKE_CURRENT_SOURCE_DIR}/.source-cache/generated/gga_repaired
    )
    if(EXISTS ${GGA_GENERATED_ASSETS}/manifest.json)
        add_test(
            NAME gga_generated_asset_validation
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_gga_problem_assets.py
                --assets ${GGA_GENERATED_ASSETS}
        )
        add_test(
            NAME gga_cpp_evaluator_oracle
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_gga_cpp_evaluator.py
                --binary $<TARGET_FILE:gga_cpp_hpc>
                --assets ${GGA_GENERATED_ASSETS}
        )
        add_test(
            NAME gga_cpp_determinism
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_gga_cpp_determinism.py
                --binary $<TARGET_FILE:gga_cpp_hpc>
                --problem
                ${GGA_GENERATED_ASSETS}/Netherlands_Egmond_aan_Zee.wfp
        )
        add_test(
            NAME geoga_cpp_declared_reconstruction
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_geoga_cpp.py
                --binary $<TARGET_FILE:gga_cpp_hpc>
                --problem
                ${GGA_GENERATED_ASSETS}/UK_London_Array.wfp
        )
        add_test(
            NAME tmoea_cpp_nysted_reconstruction
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_tmoea_cpp.py
                --binary $<TARGET_FILE:gga_cpp_hpc>
                --problem
                ${GGA_GENERATED_ASSETS}/Denmark_Nysted.wfp
        )
        add_test(
            NAME tmoea_nysted_r4_independent_oracle
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_tmoea_nysted_r4.py
                --binary $<TARGET_FILE:gga_cpp_hpc>
                --problem
                ${GGA_GENERATED_ASSETS}/Denmark_Nysted.wfp
        )
        add_test(
            NAME plan004_tmoea_nysted_r4_numerical_receipt
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/measure_tmoea_nysted_r4_equivalence.py
                --binary $<TARGET_FILE:gga_cpp_hpc>
                --problem
                ${GGA_GENERATED_ASSETS}/Denmark_Nysted.wfp
        )
    endif()
    add_test(
        NAME pbea_cpp_evaluator_oracle
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_pbea_cpp_evaluator.py
            --binary $<TARGET_FILE:pbea_cpp_hpc>
    )
    add_test(
        NAME pbea_cpp_determinism
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_pbea_cpp_determinism.py
            --binary $<TARGET_FILE:pbea_cpp_hpc>
    )
    add_test(
        NAME pbea_cpp_front_artifacts
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_pbea_cpp_fronts.py
            --binary $<TARGET_FILE:pbea_cpp_hpc>
    )
    add_test(
        NAME ppga_cpp_declared_transfer
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_ppga_cpp.py
            --binary $<TARGET_FILE:wflop_cpp_hpc>
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/shared/contracts/benchmark_cases.json
    )
    add_test(
        NAME blocked_learning_methods_guard
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_blocked_learning_methods.py
            --binary $<TARGET_FILE:wflop_cpp_hpc>
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/shared/contracts/benchmark_cases.json
    )
    add_test(
        NAME rlfode_seeded_reconstruction
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_rlfode_seeded_reconstruction.py
            --binary $<TARGET_FILE:wflop_cpp_hpc>
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/shared/contracts/benchmark_cases.json
            --models
            ${CMAKE_CURRENT_SOURCE_DIR}/shared/models/fqfode_seeded
    )
    add_test(
        NAME reconstruction_sensitivity_receipts
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_reconstruction_sensitivity.py
    )
    add_test(
        NAME performance_receipts
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_performance_receipts.py
    )
    add_test(
        NAME plan005_h6_fail_fast_science
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_plan005_h6_fail_fast.py
    )
endif()
