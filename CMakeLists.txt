cmake_minimum_required(VERSION 3.20)
project(fode_hpc_wflop VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
add_subdirectory(hpc/wflop_cpp)

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
endif()
