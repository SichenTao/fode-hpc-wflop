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

add_library(core99_t12_core src/windflo_t12.cpp)
target_include_directories(core99_t12_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t12_core PUBLIC fode_core)
target_compile_features(core99_t12_core PUBLIC cxx_std_20)
target_compile_options(core99_t12_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t12_hpc src/windflo_t12_main.cpp)
target_link_libraries(core99_t12_hpc PRIVATE core99_t12_core)

add_library(core99_t14_core src/stanley_t14.cpp)
target_include_directories(core99_t14_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t14_core PUBLIC fode_core)
target_compile_features(core99_t14_core PUBLIC cxx_std_20)
target_compile_options(core99_t14_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t14_hpc src/stanley_t14_main.cpp)
target_link_libraries(core99_t14_hpc PRIVATE core99_t14_core)

add_library(core99_t15_core src/iea37_t15.cpp)
target_include_directories(core99_t15_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t15_core PUBLIC fode_core)
target_compile_features(core99_t15_core PUBLIC cxx_std_20)
target_compile_options(core99_t15_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t15_hpc src/iea37_t15_main.cpp)
target_link_libraries(core99_t15_hpc PRIVATE core99_t15_core)

add_library(core99_t20_core src/kunakote_t20.cpp)
target_include_directories(core99_t20_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t20_core PUBLIC fode_core)
target_compile_features(core99_t20_core PUBLIC cxx_std_20)
target_compile_options(core99_t20_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t20_hpc src/kunakote_t20_main.cpp)
target_link_libraries(core99_t20_hpc PRIVATE core99_t20_core)

add_library(core99_t48_core src/lackner_t48.cpp)
target_include_directories(core99_t48_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t48_core PUBLIC fode_core)
target_compile_features(core99_t48_core PUBLIC cxx_std_20)
target_compile_options(core99_t48_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t48_hpc src/lackner_t48_main.cpp)
target_link_libraries(core99_t48_hpc PRIVATE core99_t48_core)

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
    add_executable(core99_t12_test tests/windflo_t12_test.cpp)
    target_link_libraries(core99_t12_test PRIVATE core99_t12_core)
    add_test(NAME core99_t12_cpp COMMAND core99_t12_test)
    add_test(
        NAME core99_t12_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t12.py
            --binary $<TARGET_FILE:core99_t12_hpc>
            --contract
                ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/core99_t12_windflo_2015.json
    )
    add_executable(core99_t14_test tests/stanley_t14_test.cpp)
    target_link_libraries(core99_t14_test PRIVATE core99_t14_core)
    add_test(NAME core99_t14_cpp COMMAND core99_t14_test)
    add_test(
        NAME core99_t14_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t14.py
            --binary $<TARGET_FILE:core99_t14_hpc>
            --contract
                ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/core99_t14_stanley_2019.json
            --resource-data
                ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_t14_wind_resources.json
    )
    add_executable(core99_t15_test tests/iea37_t15_test.cpp)
    target_link_libraries(core99_t15_test PRIVATE core99_t15_core)
    add_test(NAME core99_t15_cpp COMMAND core99_t15_test)
    add_test(
        NAME core99_t15_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t15.py
            --binary $<TARGET_FILE:core99_t15_hpc>
            --contract
                ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/contracts/core99_t15_iea37_2019.json
            --resource-data
                ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_t15_iea37_cs1.json
    )
    add_executable(core99_t20_test tests/kunakote_t20_test.cpp)
    target_link_libraries(core99_t20_test PRIVATE core99_t20_core)
    add_test(NAME core99_t20_cpp COMMAND core99_t20_test)
    add_test(
        NAME core99_t20_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t20.py
            --binary $<TARGET_FILE:core99_t20_hpc>
    )
    add_executable(core99_t48_test tests/lackner_t48_test.cpp)
    target_link_libraries(core99_t48_test PRIVATE core99_t48_core)
    add_test(NAME core99_t48_cpp COMMAND core99_t48_test)
    add_test(
        NAME core99_t48_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t48.py
            --binary $<TARGET_FILE:core99_t48_hpc>
    )
endif()
