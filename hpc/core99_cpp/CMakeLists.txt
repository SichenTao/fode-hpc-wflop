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

add_library(core99_l0124_core src/parada_l0124.cpp)
target_include_directories(core99_l0124_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_l0124_core PUBLIC fode_core)
target_compile_features(core99_l0124_core PUBLIC cxx_std_20)
target_compile_options(core99_l0124_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_l0124_hpc src/parada_l0124_main.cpp)
target_link_libraries(core99_l0124_hpc PRIVATE core99_l0124_core)

add_library(core99_t87_core src/hu_t87.cpp)
target_include_directories(core99_t87_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t87_core PUBLIC fode_core)
target_compile_features(core99_t87_core PUBLIC cxx_std_20)
target_compile_options(core99_t87_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t87_hpc src/hu_t87_main.cpp)
target_link_libraries(core99_t87_hpc PRIVATE core99_t87_core)

add_library(core99_l0371_core src/guo_l0371.cpp)
target_include_directories(core99_l0371_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_l0371_core PUBLIC fode_core)
target_compile_features(core99_l0371_core PUBLIC cxx_std_20)
target_compile_options(core99_l0371_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_l0371_hpc src/guo_l0371_main.cpp)
target_link_libraries(core99_l0371_hpc PRIVATE core99_l0371_core)

add_library(core99_l0499_core src/wen_l0499.cpp)
target_include_directories(core99_l0499_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_l0499_core PUBLIC fode_core)
target_compile_features(core99_l0499_core PUBLIC cxx_std_20)
target_compile_options(core99_l0499_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_l0499_hpc src/wen_l0499_main.cpp)
target_link_libraries(core99_l0499_hpc PRIVATE core99_l0499_core)

add_library(core99_l0590_core src/sun_l0590.cpp)
target_include_directories(core99_l0590_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_l0590_core PUBLIC fode_core)
target_compile_features(core99_l0590_core PUBLIC cxx_std_20)
target_compile_options(core99_l0590_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_l0590_hpc src/sun_l0590_main.cpp)
target_link_libraries(core99_l0590_hpc PRIVATE core99_l0590_core)

add_library(core99_l0341_core src/tao_l0341.cpp)
target_include_directories(core99_l0341_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_l0341_core PUBLIC fode_core)
target_compile_features(core99_l0341_core PUBLIC cxx_std_20)
target_compile_options(core99_l0341_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_l0341_hpc src/tao_l0341_main.cpp)
target_link_libraries(core99_l0341_hpc PRIVATE core99_l0341_core)

add_library(core99_l0623_core src/wang_l0623.cpp)
target_include_directories(core99_l0623_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_l0623_core PUBLIC fode_core)
target_compile_features(core99_l0623_core PUBLIC cxx_std_20)
target_compile_options(core99_l0623_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_l0623_hpc src/wang_l0623_main.cpp)
target_link_libraries(core99_l0623_hpc PRIVATE core99_l0623_core)

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

add_library(core99_t62_core src/gao_t62.cpp)
target_include_directories(core99_t62_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t62_core PUBLIC fode_core)
target_compile_features(core99_t62_core PUBLIC cxx_std_20)
target_compile_options(core99_t62_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t62_hpc src/gao_t62_main.cpp)
target_link_libraries(core99_t62_hpc PRIVATE core99_t62_core)

add_library(core99_t17_core src/brogna_t17.cpp)
target_include_directories(core99_t17_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t17_core PUBLIC fode_core)
target_compile_features(core99_t17_core PUBLIC cxx_std_20)
target_compile_options(core99_t17_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t17_hpc src/brogna_t17_main.cpp)
target_link_libraries(core99_t17_hpc PRIVATE core99_t17_core)

option(
    CORE99_ENABLE_T63
    "Build T63 with the pinned HiGHS C++ MILP backend"
    OFF
)
if(CORE99_ENABLE_T63)
    include(FetchContent)
    set(CORE99_SAVED_BUILD_TESTING ${BUILD_TESTING})
    set(FAST_BUILD ON CACHE BOOL "" FORCE)
    set(BUILD_CXX ON CACHE BOOL "" FORCE)
    set(BUILD_CXX_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_EXTRAS_LIB OFF CACHE BOOL "" FORCE)
    set(ZLIB OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        highs
        GIT_REPOSITORY https://github.com/ERGO-Code/HiGHS.git
        GIT_TAG 04024d701f79feb8e2f18bc3df0dffc04ef05088
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(highs)
    set(BUILD_TESTING ${CORE99_SAVED_BUILD_TESTING} CACHE BOOL "" FORCE)

    add_library(core99_t63_core STATIC src/kuo_t63.cpp)
    target_include_directories(core99_t63_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t63_core PUBLIC fode_core highs::highs)
    target_compile_features(core99_t63_core PUBLIC cxx_std_20)
    target_compile_options(core99_t63_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t63_hpc src/kuo_t63_main.cpp)
    target_link_libraries(core99_t63_hpc PRIVATE core99_t63_core)
endif()

option(
    CORE99_ENABLE_T16
    "Build T16 with pinned NLopt SLSQP and autodiff reverse mode"
    OFF
)
if(CORE99_ENABLE_T16)
    include(FetchContent)

    set(NLOPT_CXX ON CACHE BOOL "" FORCE)
    set(NLOPT_FORTRAN OFF CACHE BOOL "" FORCE)
    set(NLOPT_PYTHON OFF CACHE BOOL "" FORCE)
    set(NLOPT_OCTAVE OFF CACHE BOOL "" FORCE)
    set(NLOPT_MATLAB OFF CACHE BOOL "" FORCE)
    set(NLOPT_GUILE OFF CACHE BOOL "" FORCE)
    set(NLOPT_JAVA OFF CACHE BOOL "" FORCE)
    set(NLOPT_SWIG OFF CACHE BOOL "" FORCE)
    set(NLOPT_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        nlopt
        GIT_REPOSITORY https://github.com/stevengj/nlopt.git
        GIT_TAG 88c424d4f458412787df96fcc95218acbca224fd
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(nlopt)

    add_library(core99_t16_core STATIC src/thomas_t16.cpp)
    target_include_directories(core99_t16_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(
        core99_t16_core
        PUBLIC fode_core nlopt
    )
    target_compile_features(core99_t16_core PUBLIC cxx_std_20)
    target_compile_options(core99_t16_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t16_hpc src/thomas_t16_main.cpp)
    target_link_libraries(core99_t16_hpc PRIVATE core99_t16_core)
endif()

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(core99_l0124_test tests/parada_l0124_test.cpp)
    target_link_libraries(core99_l0124_test PRIVATE core99_l0124_core)
    add_test(NAME core99_l0124_cpp COMMAND core99_l0124_test)
    add_test(
        NAME core99_l0124_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_l0124.py
            --binary $<TARGET_FILE:core99_l0124_hpc>
    )
    add_executable(core99_t87_test tests/hu_t87_test.cpp)
    target_link_libraries(core99_t87_test PRIVATE core99_t87_core)
    add_test(
        NAME core99_t87_cpp
        COMMAND core99_t87_test
            ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_t87_figure_proxy.bin
    )
    add_test(
        NAME core99_t87_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t87.py
            --binary $<TARGET_FILE:core99_t87_hpc>
            --data
                ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_t87_figure_proxy.bin
    )
    add_executable(core99_l0371_test tests/guo_l0371_test.cpp)
    target_link_libraries(core99_l0371_test PRIVATE core99_l0371_core)
    add_test(
        NAME core99_l0371_cpp
        COMMAND core99_l0371_test
            ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_l0371_proxy.bin
    )
    add_test(
        NAME core99_l0371_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_l0371.py
            --binary $<TARGET_FILE:core99_l0371_hpc>
            --data
                ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_l0371_proxy.bin
    )
    add_executable(core99_l0499_test tests/wen_l0499_test.cpp)
    target_link_libraries(core99_l0499_test PRIVATE core99_l0499_core)
    add_test(
        NAME core99_l0499_cpp
        COMMAND core99_l0499_test
            ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_l0499_proxy.bin
    )
    add_test(
        NAME core99_l0499_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_l0499.py
            --binary $<TARGET_FILE:core99_l0499_hpc>
            --data
                ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_l0499_proxy.bin
    )
    add_executable(core99_l0590_test tests/sun_l0590_test.cpp)
    target_link_libraries(core99_l0590_test PRIVATE core99_l0590_core)
    add_test(NAME core99_l0590_cpp COMMAND core99_l0590_test)
    add_test(
        NAME core99_l0590_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_l0590.py
            --binary $<TARGET_FILE:core99_l0590_hpc>
    )
    add_executable(core99_l0341_test tests/tao_l0341_test.cpp)
    target_link_libraries(core99_l0341_test PRIVATE core99_l0341_core)
    add_test(NAME core99_l0341_cpp COMMAND core99_l0341_test)
    add_test(
        NAME core99_l0341_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_l0341.py
            --binary $<TARGET_FILE:core99_l0341_hpc>
    )
    add_executable(core99_l0623_test tests/wang_l0623_test.cpp)
    target_link_libraries(core99_l0623_test PRIVATE core99_l0623_core)
    add_test(NAME core99_l0623_cpp COMMAND core99_l0623_test)
    add_test(
        NAME core99_l0623_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_l0623.py
            --binary $<TARGET_FILE:core99_l0623_hpc>
    )
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
    add_executable(core99_t62_test tests/gao_t62_test.cpp)
    target_link_libraries(core99_t62_test PRIVATE core99_t62_core)
    add_test(NAME core99_t62_cpp COMMAND core99_t62_test)
    add_test(
        NAME core99_t62_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t62.py
            --binary $<TARGET_FILE:core99_t62_hpc>
    )
    add_executable(core99_t17_test tests/brogna_t17_test.cpp)
    target_link_libraries(core99_t17_test PRIVATE core99_t17_core)
    add_test(NAME core99_t17_cpp COMMAND core99_t17_test)
    add_test(
        NAME core99_t17_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t17.py
            --binary $<TARGET_FILE:core99_t17_hpc>
            --proxy
                ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_t17_pywake_proxy.bin
    )
    if(CORE99_ENABLE_T63)
        add_executable(core99_t63_test tests/kuo_t63_test.cpp)
        target_link_libraries(core99_t63_test PRIVATE core99_t63_core)
        add_test(
            NAME core99_t63_cpp
            COMMAND core99_t63_test
                ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_t63_figure_proxy.bin
        )
        add_test(
            NAME core99_t63_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t63.py
                --binary $<TARGET_FILE:core99_t63_hpc>
                --proxy
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_t63_figure_proxy.bin
        )
    endif()
    if(CORE99_ENABLE_T16)
        add_executable(core99_t16_test tests/thomas_t16_test.cpp)
        target_link_libraries(core99_t16_test PRIVATE core99_t16_core)
        add_test(
            NAME core99_t16_cpp
            COMMAND core99_t16_test
                ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_t16_public_data.bin
        )
        add_test(
            NAME core99_t16_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t16.py
                --binary $<TARGET_FILE:core99_t16_hpc>
                --data
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_t16_public_data.bin
        )
    endif()
endif()
