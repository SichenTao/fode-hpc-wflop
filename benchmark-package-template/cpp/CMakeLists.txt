cmake_minimum_required(VERSION 3.20)
project(wflop_benchmark_package_template LANGUAGES CXX)

include(CTest)

add_library(
    benchmark_package_template
    STATIC
    src/backend.cpp
    src/registry.cpp
    src/example_algorithm.cpp
    src/example_problem.cpp
)
target_compile_features(benchmark_package_template PUBLIC cxx_std_20)
target_include_directories(
    benchmark_package_template
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(benchmark_package_runner src/main.cpp)
target_link_libraries(
    benchmark_package_runner
    PRIVATE benchmark_package_template
)

if(BUILD_TESTING)
    foreach(category IN ITEMS semantic sensitivity scaling formal_campaign)
        add_executable(
            benchmark_template_${category}_test
            tests/${category}_test.cpp
        )
        target_link_libraries(
            benchmark_template_${category}_test
            PRIVATE benchmark_package_template
        )
        add_test(
            NAME benchmark_template_${category}
            COMMAND benchmark_template_${category}_test
        )
        set_tests_properties(
            benchmark_template_${category}
            PROPERTIES LABELS ${category}
        )
    endforeach()
endif()
