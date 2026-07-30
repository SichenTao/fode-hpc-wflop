add_library(core99_historical_grid_core
    src/historical_grid.cpp
)
target_include_directories(core99_historical_grid_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_historical_grid_core PUBLIC fode_core)
target_compile_features(core99_historical_grid_core PUBLIC cxx_std_20)
target_compile_options(core99_historical_grid_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)

add_executable(core99_historical_grid_hpc src/main.cpp)
target_link_libraries(
    core99_historical_grid_hpc
    PRIVATE core99_historical_grid_core
)

add_library(core99_t03_core src/kusiak_t03.cpp)
target_include_directories(core99_t03_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t03_core PUBLIC fode_core)
target_compile_features(core99_t03_core PUBLIC cxx_std_20)
target_compile_options(core99_t03_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t03_hpc src/kusiak_t03_main.cpp)
target_link_libraries(core99_t03_hpc PRIVATE core99_t03_core)

add_library(core99_t04_core src/uwflo_t04.cpp)
target_include_directories(core99_t04_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t04_core PUBLIC fode_core)
target_compile_features(core99_t04_core PUBLIC cxx_std_20)
target_compile_options(core99_t04_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t04_hpc src/uwflo_t04_main.cpp)
target_link_libraries(core99_t04_hpc PRIVATE core99_t04_core)

add_library(core99_t22_core src/debo_t22.cpp)
target_include_directories(core99_t22_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t22_core PUBLIC fode_core)
target_compile_features(core99_t22_core PUBLIC cxx_std_20)
target_compile_options(core99_t22_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t22_hpc src/debo_t22_main.cpp)
target_link_libraries(core99_t22_hpc PRIVATE core99_t22_core)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(
        core99_historical_grid_test
        tests/historical_grid_test.cpp
    )
    target_link_libraries(
        core99_historical_grid_test
        PRIVATE core99_historical_grid_core
    )
    add_test(
        NAME core99_historical_grid_cpp
        COMMAND core99_historical_grid_test
    )
    add_test(
        NAME core99_historical_grid_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_historical_grid.py
            --binary $<TARGET_FILE:core99_historical_grid_hpc>
    )
    add_executable(core99_t03_test tests/kusiak_t03_test.cpp)
    target_link_libraries(core99_t03_test PRIVATE core99_t03_core)
    add_test(NAME core99_t03_cpp COMMAND core99_t03_test)
    add_test(
        NAME core99_t03_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t03.py
            --binary $<TARGET_FILE:core99_t03_hpc>
    )
    add_executable(core99_t04_test tests/uwflo_t04_test.cpp)
    target_link_libraries(core99_t04_test PRIVATE core99_t04_core)
    add_test(NAME core99_t04_cpp COMMAND core99_t04_test)
    add_test(
        NAME core99_t04_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t04.py
            --binary $<TARGET_FILE:core99_t04_hpc>
    )
    add_executable(core99_t22_test tests/debo_t22_test.cpp)
    target_link_libraries(core99_t22_test PRIVATE core99_t22_core)
    add_test(NAME core99_t22_cpp COMMAND core99_t22_test)
    add_test(
        NAME core99_t22_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t22.py
            --binary $<TARGET_FILE:core99_t22_hpc>
            --contract
                ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/core99_t22_iea37_cs4.json
    )
endif()
