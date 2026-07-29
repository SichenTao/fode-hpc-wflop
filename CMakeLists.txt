cmake_minimum_required(VERSION 3.20)
project(fode_hpc_wflop VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
add_subdirectory(hpc/wflop_cpp)
add_subdirectory(hpc/gga_cpp)
add_subdirectory(hpc/pbea_cpp)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
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
        NAME source_dossiers
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_source_dossiers.py
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
endif()
