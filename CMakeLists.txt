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
        NAME problem_package_registry
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_problem_packages.py
    )
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
endif()
