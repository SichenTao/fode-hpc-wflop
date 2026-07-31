add_library(core99_historical_grid_core STATIC
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

add_library(core99_l0124_core STATIC src/parada_l0124.cpp)
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

add_library(core99_t87_core STATIC src/hu_t87.cpp)
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

add_library(core99_l0371_core STATIC src/guo_l0371.cpp)
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

add_library(core99_l0499_core STATIC src/wen_l0499.cpp)
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

add_library(core99_l0590_core STATIC src/sun_l0590.cpp)
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

add_library(core99_l0341_core STATIC src/tao_l0341.cpp)
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

add_library(core99_l0623_core STATIC src/wang_l0623.cpp)
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

add_library(core99_t74_core STATIC src/ju_t74.cpp)
target_include_directories(core99_t74_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t74_core PUBLIC fode_core)
target_compile_features(core99_t74_core PUBLIC cxx_std_20)
target_compile_options(core99_t74_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t74_hpc src/ju_t74_main.cpp)
target_link_libraries(core99_t74_hpc PRIVATE core99_t74_core)

add_library(core99_discrete_grid_wake_core STATIC src/discrete_grid_wake.cpp)
target_include_directories(core99_discrete_grid_wake_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(core99_discrete_grid_wake_core PUBLIC cxx_std_20)
target_compile_options(core99_discrete_grid_wake_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)

add_library(core99_t80_core STATIC src/bai_t80.cpp)
target_include_directories(core99_t80_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(
    core99_t80_core
    PUBLIC fode_core core99_discrete_grid_wake_core
)
target_compile_features(core99_t80_core PUBLIC cxx_std_20)
target_compile_options(core99_t80_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t80_hpc src/bai_t80_main.cpp)
target_link_libraries(core99_t80_hpc PRIVATE core99_t80_core)

add_library(core99_t05_core STATIC src/turner_t05.cpp)
target_include_directories(core99_t05_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t05_core PUBLIC fode_core)
target_compile_features(core99_t05_core PUBLIC cxx_std_20)
target_compile_options(core99_t05_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t05_hpc src/turner_t05_main.cpp)
target_link_libraries(core99_t05_hpc PRIVATE core99_t05_core)

add_library(core99_t03_core STATIC src/kusiak_t03.cpp)
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

add_library(core99_t04_core STATIC src/uwflo_t04.cpp)
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

add_library(core99_t22_core STATIC src/debo_t22.cpp)
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

add_library(core99_t12_core STATIC src/windflo_t12.cpp)
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

add_library(core99_t14_core STATIC src/stanley_t14.cpp)
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

add_library(core99_t15_core STATIC src/iea37_t15.cpp)
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

add_library(core99_t20_core STATIC src/kunakote_t20.cpp)
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

add_library(core99_t48_core STATIC src/lackner_t48.cpp)
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

add_library(core99_t62_core STATIC src/gao_t62.cpp)
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

add_library(core99_t17_core STATIC src/brogna_t17.cpp)
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

add_library(core99_t77_core STATIC src/long_t77.cpp)
target_include_directories(core99_t77_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
)
target_link_libraries(core99_t77_core PUBLIC fode_core)
target_compile_features(core99_t77_core PUBLIC cxx_std_20)
target_compile_options(core99_t77_core PRIVATE
    -O3
    -march=native
    -ffp-contract=off
)
add_executable(core99_t77_hpc src/long_t77_main.cpp)
target_link_libraries(core99_t77_hpc PRIVATE core99_t77_core)

option(
    CORE99_ENABLE_T31
    "Build T31 with the official DTU large-offshore dataset"
    OFF
)
option(
    CORE99_ENABLE_T33
    "Build T33 combined layout/cable VNS with the official DTU dataset"
    OFF
)
option(
    CORE99_ENABLE_T83
    "Build T83 multi-scale offshore WFASP with the official DTU proxy data"
    OFF
)
if(CORE99_ENABLE_T31 OR CORE99_ENABLE_T33 OR CORE99_ENABLE_T83)
    set(
        CORE99_T31_ARCHIVE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/data/core99_t31/VNSforLargeOffshoreWindFarmLayoutOptimization_SyntheticInstances.zip
    )
    set(CORE99_T31_EXTRACT_BASE ${CMAKE_BINARY_DIR}/core99_t31_data)
    set(
        CORE99_T31_DATA_ROOT
        ${CORE99_T31_EXTRACT_BASE}/VNSforLargeOffshoreWindFarmLayoutOptimization_SyntheticInstances
    )
    if(NOT EXISTS ${CORE99_T31_DATA_ROOT}/README.md)
        file(MAKE_DIRECTORY ${CORE99_T31_EXTRACT_BASE})
        file(
            ARCHIVE_EXTRACT
            INPUT ${CORE99_T31_ARCHIVE}
            DESTINATION ${CORE99_T31_EXTRACT_BASE}
        )
    endif()
    add_library(core99_t31_core STATIC src/cazzaro_t31.cpp)
    target_include_directories(core99_t31_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t31_core PUBLIC fode_core)
    target_compile_features(core99_t31_core PUBLIC cxx_std_20)
    target_compile_options(core99_t31_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
endif()
if(CORE99_ENABLE_T31)
    add_executable(core99_t31_hpc src/cazzaro_t31_main.cpp)
    target_link_libraries(core99_t31_hpc PRIVATE core99_t31_core)
endif()
if(CORE99_ENABLE_T33)
    add_library(core99_t33_core STATIC src/cazzaro_t33.cpp)
    target_include_directories(core99_t33_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t33_core PUBLIC core99_t31_core fode_core)
    target_compile_features(core99_t33_core PUBLIC cxx_std_20)
    target_compile_options(core99_t33_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t33_hpc src/cazzaro_t33_main.cpp)
    target_link_libraries(core99_t33_hpc PRIVATE core99_t33_core)
endif()
if(CORE99_ENABLE_T83)
    add_library(core99_t83_core STATIC src/cazzaro_t83.cpp)
    target_include_directories(core99_t83_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t83_core PUBLIC core99_t31_core fode_core)
    target_compile_features(core99_t83_core PUBLIC cxx_std_20)
    target_compile_options(core99_t83_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t83_hpc src/cazzaro_t83_main.cpp)
    target_link_libraries(core99_t83_hpc PRIVATE core99_t83_core)
endif()

option(
    CORE99_ENABLE_T63
    "Build T63 with the pinned HiGHS C++ MILP backend"
    OFF
)
option(
    CORE99_ENABLE_T30
    "Build T30 proximity search with the pinned HiGHS C++ MILP backend"
    OFF
)
if(CORE99_ENABLE_T63 OR CORE99_ENABLE_T30)
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
endif()

if(CORE99_ENABLE_T63)
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

if(CORE99_ENABLE_T30)
    add_library(core99_t30_core STATIC src/fischetti_t30.cpp)
    target_include_directories(core99_t30_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t30_core PUBLIC fode_core highs::highs)
    target_compile_features(core99_t30_core PUBLIC cxx_std_20)
    target_compile_options(core99_t30_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t30_hpc src/fischetti_t30_main.cpp)
    target_link_libraries(core99_t30_hpc PRIVATE core99_t30_core)
    if(BUILD_TESTING)
        add_executable(core99_t30_test tests/fischetti_t30_test.cpp)
        target_link_libraries(core99_t30_test PRIVATE core99_t30_core)
        add_test(NAME core99_t30_cpp_highs COMMAND core99_t30_test)
        set_tests_properties(core99_t30_cpp_highs PROPERTIES TIMEOUT 300)
    endif()
endif()

option(
    CORE99_ENABLE_T16
    "Build T16 with pinned NLopt SLSQP and autodiff reverse mode"
    OFF
)
option(
    CORE99_ENABLE_T07
    "Build T07 explicit SCP with pinned NLopt convex-QP replacement"
    OFF
)
option(
    CORE99_ENABLE_T21
    "Build T21 density-topology WFLOP with pinned NLopt MMA"
    OFF
)
option(
    CORE99_ENABLE_T72
    "Build T72 CHCP-NSGA-II energy-noise WFLOP reconstruction"
    OFF
)
option(
    CORE99_ENABLE_T64
    "Build T64 land-constrained energy-noise NSGA-II reproduction"
    OFF
)
option(
    CORE99_ENABLE_T67
    "Build T67 commercial-turbine and hub-height MATLAB-ga reproduction"
    OFF
)
option(
    CORE99_ENABLE_T76
    "Build T76 directional heterogeneous-turbine MPGA reproduction"
    OFF
)
option(
    CORE99_ENABLE_T81
    "Build T81 inhomogeneous-wave Multistart-SLSQP reproduction"
    OFF
)
option(
    CORE99_ENABLE_T68
    "Build T68 FINO3 layout/power-dispatch APSO reproduction"
    OFF
)
option(
    CORE99_ENABLE_T78
    "Build T78 onshore noise-layout traditional-PSO reproduction"
    OFF
)
option(
    CORE99_ENABLE_T60
    "Build T60 improved random-search incremental WFLOP reproduction"
    OFF
)
option(
    CORE99_ENABLE_T69
    "Build T69 changing-wind robustness CPU-HPC reproduction"
    OFF
)
option(
    CORE99_ENABLE_L0079
    "Build L0079 Middelgrunden GA/PSO CPU-HPC reproduction"
    OFF
)
option(
    CORE99_ENABLE_Y09
    "Build Y09 multi-type turbine LCOE GA CPU-HPC reproduction"
    OFF
)
option(
    CORE99_ENABLE_Y14
    "Build Y14 preference-oriented energy-noise p-SDRDE CPU-HPC reproduction"
    OFF
)
option(
    CORE99_ENABLE_T11
    "Build T11 BlockCopy four-method/four-problem HPC reproduction"
    OFF
)
option(
    CORE99_ENABLE_L0259
    "Build L0259 SUGGA 117-problem CPU-HPC reproduction"
    OFF
)
option(
    CORE99_ENABLE_T82
    "Build T82 power-turbulence NSGA-II CPU-HPC reproduction"
    OFF
)
option(
    CORE99_ENABLE_T85
    "Build T85 joint-layout/yaw AGLDPSO CPU-HPC reproduction"
    OFF
)
option(
    CORE99_ENABLE_T24
    "Build T24 Markov-intermittency NSGA-III CPU-HPC reproduction"
    OFF
)
if(CORE99_ENABLE_T16 OR CORE99_ENABLE_T07 OR CORE99_ENABLE_T21
   OR CORE99_ENABLE_T81)
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

endif()

if(CORE99_ENABLE_T16)
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

if(CORE99_ENABLE_T07)
    add_library(core99_t07_core STATIC src/park_t07.cpp)
    target_include_directories(core99_t07_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t07_core PUBLIC fode_core nlopt)
    target_compile_features(core99_t07_core PUBLIC cxx_std_20)
    target_compile_options(core99_t07_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t07_hpc src/park_t07_main.cpp)
    target_link_libraries(core99_t07_hpc PRIVATE core99_t07_core)
endif()

if(CORE99_ENABLE_T21)
    add_library(core99_t21_core STATIC src/pollini_t21.cpp)
    target_include_directories(core99_t21_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t21_core PUBLIC fode_core nlopt)
    target_compile_features(core99_t21_core PUBLIC cxx_std_20)
    target_compile_options(core99_t21_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t21_hpc src/pollini_t21_main.cpp)
    target_link_libraries(core99_t21_hpc PRIVATE core99_t21_core)
endif()

if(CORE99_ENABLE_T81)
    add_library(core99_t81_core STATIC src/ti_t81.cpp)
    target_include_directories(core99_t81_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t81_core PUBLIC fode_core nlopt)
    target_compile_features(core99_t81_core PUBLIC cxx_std_20)
    target_compile_options(core99_t81_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t81_hpc src/ti_t81_main.cpp)
    target_link_libraries(core99_t81_hpc PRIVATE core99_t81_core)
endif()

if(CORE99_ENABLE_T68)
    add_library(core99_t68_core STATIC src/hou_t68.cpp)
    target_include_directories(core99_t68_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t68_core PUBLIC fode_core)
    target_compile_features(core99_t68_core PUBLIC cxx_std_20)
    target_compile_options(core99_t68_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t68_hpc src/hou_t68_main.cpp)
    target_link_libraries(core99_t68_hpc PRIVATE core99_t68_core)
endif()

if(CORE99_ENABLE_T78)
    add_library(core99_t78_core STATIC src/wu_t78.cpp)
    target_include_directories(core99_t78_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t78_core PUBLIC fode_core)
    target_compile_features(core99_t78_core PUBLIC cxx_std_20)
    target_compile_options(core99_t78_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t78_hpc src/wu_t78_main.cpp)
    target_link_libraries(core99_t78_hpc PRIVATE core99_t78_core)
endif()

if(CORE99_ENABLE_T72 OR CORE99_ENABLE_T64)
    add_library(core99_t72_core STATIC src/sorkhabi_t72.cpp)
    target_include_directories(core99_t72_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t72_core PUBLIC fode_core)
    target_compile_features(core99_t72_core PUBLIC cxx_std_20)
    target_compile_options(core99_t72_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    if(CORE99_ENABLE_T72)
        add_executable(core99_t72_hpc src/sorkhabi_t72_main.cpp)
        target_link_libraries(core99_t72_hpc PRIVATE core99_t72_core)
    endif()
endif()

if(CORE99_ENABLE_T64)
    add_library(core99_t64_core STATIC src/sorkhabi_t64.cpp)
    target_include_directories(core99_t64_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t64_core PUBLIC core99_t72_core)
    target_compile_features(core99_t64_core PUBLIC cxx_std_20)
    target_compile_options(core99_t64_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t64_hpc src/sorkhabi_t64_main.cpp)
    target_link_libraries(core99_t64_hpc PRIVATE core99_t64_core)
endif()

if(CORE99_ENABLE_T67)
    add_library(core99_t67_core STATIC src/abdulrahman_t67.cpp)
    target_include_directories(core99_t67_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t67_core PUBLIC fode_core)
    target_compile_features(core99_t67_core PUBLIC cxx_std_20)
    target_compile_options(core99_t67_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t67_hpc src/abdulrahman_t67_main.cpp)
    target_link_libraries(core99_t67_hpc PRIVATE core99_t67_core)
endif()

if(CORE99_ENABLE_T76)
    add_library(core99_t76_core STATIC src/sun_t76.cpp)
    target_include_directories(core99_t76_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t76_core PUBLIC fode_core)
    target_compile_features(core99_t76_core PUBLIC cxx_std_20)
    target_compile_options(core99_t76_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t76_hpc src/sun_t76_main.cpp)
    target_link_libraries(core99_t76_hpc PRIVATE core99_t76_core)
endif()

if(CORE99_ENABLE_T60)
    add_library(core99_t60_core STATIC src/feng_t60.cpp)
    target_include_directories(core99_t60_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t60_core PUBLIC fode_core)
    target_compile_features(core99_t60_core PUBLIC cxx_std_20)
    target_compile_options(core99_t60_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t60_hpc src/feng_t60_main.cpp)
    target_link_libraries(core99_t60_hpc PRIVATE core99_t60_core)
endif()

if(CORE99_ENABLE_T69)
    add_library(core99_t69_core STATIC src/feng_t69.cpp)
    target_include_directories(core99_t69_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t69_core PUBLIC fode_core)
    target_compile_features(core99_t69_core PUBLIC cxx_std_20)
    target_compile_options(core99_t69_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t69_hpc src/feng_t69_main.cpp)
    target_link_libraries(core99_t69_hpc PRIVATE core99_t69_core)
endif()

if(CORE99_ENABLE_L0079)
    add_library(core99_l0079_core STATIC src/pillai_l0079.cpp)
    target_include_directories(core99_l0079_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_l0079_core PUBLIC fode_core)
    target_compile_features(core99_l0079_core PUBLIC cxx_std_20)
    target_compile_options(core99_l0079_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_l0079_hpc src/pillai_l0079_main.cpp)
    target_link_libraries(core99_l0079_hpc PRIVATE core99_l0079_core)
endif()

if(CORE99_ENABLE_Y09)
    add_library(core99_y09_core STATIC src/li_y09.cpp)
    target_include_directories(core99_y09_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_y09_core PUBLIC fode_core)
    target_compile_features(core99_y09_core PUBLIC cxx_std_20)
    target_compile_options(core99_y09_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_y09_hpc src/li_y09_main.cpp)
    target_link_libraries(core99_y09_hpc PRIVATE core99_y09_core)
endif()

if(CORE99_ENABLE_Y14)
    add_library(core99_y14_core STATIC src/zhang_y14.cpp)
    target_include_directories(core99_y14_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_y14_core PUBLIC fode_core)
    target_compile_features(core99_y14_core PUBLIC cxx_std_20)
    target_compile_options(core99_y14_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_y14_hpc src/zhang_y14_main.cpp)
    target_link_libraries(core99_y14_hpc PRIVATE core99_y14_core)
endif()

if(CORE99_ENABLE_T11)
    add_library(core99_t11_core STATIC src/blockcopy_t11.cpp)
    target_include_directories(core99_t11_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t11_core PUBLIC fode_core)
    target_compile_features(core99_t11_core PUBLIC cxx_std_20)
    target_compile_options(core99_t11_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t11_hpc src/blockcopy_t11_main.cpp)
    target_link_libraries(core99_t11_hpc PRIVATE core99_t11_core)
endif()

if(CORE99_ENABLE_L0259)
    add_library(core99_l0259_libsvm STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/libsvm-l0259/svm.cpp
    )
    target_include_directories(core99_l0259_libsvm
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/libsvm-l0259
    )
    target_compile_features(core99_l0259_libsvm PUBLIC cxx_std_20)
    target_compile_options(core99_l0259_libsvm PRIVATE -O3 -w)
    add_library(core99_l0259_core STATIC src/ju_l0259.cpp)
    target_include_directories(core99_l0259_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_l0259_core
        PUBLIC
            fode_core
            core99_l0259_libsvm
    )
    target_compile_features(core99_l0259_core PUBLIC cxx_std_20)
    target_compile_options(core99_l0259_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_l0259_hpc src/ju_l0259_main.cpp)
    target_link_libraries(core99_l0259_hpc PRIVATE core99_l0259_core)
endif()

if(CORE99_ENABLE_T82)
    add_library(core99_t82_core STATIC src/cao_t82.cpp)
    target_include_directories(core99_t82_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t82_core PUBLIC fode_core)
    target_compile_features(core99_t82_core PUBLIC cxx_std_20)
    target_compile_options(core99_t82_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t82_hpc src/cao_t82_main.cpp)
    target_link_libraries(core99_t82_hpc PRIVATE core99_t82_core)
endif()

if(CORE99_ENABLE_T85)
    add_library(core99_t85_core STATIC src/song_t85.cpp)
    target_include_directories(core99_t85_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t85_core PUBLIC fode_core)
    target_compile_features(core99_t85_core PUBLIC cxx_std_20)
    target_compile_options(core99_t85_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t85_hpc src/song_t85_main.cpp)
    target_link_libraries(core99_t85_hpc PRIVATE core99_t85_core)
endif()

if(CORE99_ENABLE_T24)
    add_library(core99_t24_core STATIC src/kim_t24.cpp)
    target_include_directories(core99_t24_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../fode_cpp/include
    )
    target_link_libraries(core99_t24_core PUBLIC fode_core)
    target_compile_features(core99_t24_core PUBLIC cxx_std_20)
    target_compile_options(core99_t24_core PRIVATE
        -O3
        -march=native
        -ffp-contract=off
    )
    add_executable(core99_t24_hpc src/kim_t24_main.cpp)
    target_link_libraries(core99_t24_hpc PRIVATE core99_t24_core)
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
    add_executable(core99_t74_test tests/ju_t74_test.cpp)
    target_link_libraries(core99_t74_test PRIVATE core99_t74_core)
    add_test(NAME core99_t74_cpp COMMAND core99_t74_test)
    add_test(
        NAME core99_t74_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t74.py
            --binary $<TARGET_FILE:core99_t74_hpc>
    )
    add_executable(core99_t80_test tests/bai_t80_test.cpp)
    target_link_libraries(core99_t80_test PRIVATE core99_t80_core)
    add_test(NAME core99_t80_cpp COMMAND core99_t80_test)
    add_test(
        NAME core99_t80_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t80.py
            --binary $<TARGET_FILE:core99_t80_hpc>
    )
    add_executable(core99_t05_test tests/turner_t05_test.cpp)
    target_link_libraries(core99_t05_test PRIVATE core99_t05_core)
    add_test(NAME core99_t05_cpp COMMAND core99_t05_test)
    add_test(
        NAME core99_t05_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t05.py
            --binary $<TARGET_FILE:core99_t05_hpc>
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
    if(CORE99_ENABLE_T07)
        add_executable(core99_t07_test tests/park_t07_test.cpp)
        target_link_libraries(core99_t07_test PRIVATE core99_t07_core)
        add_test(NAME core99_t07_cpp COMMAND core99_t07_test)
        add_test(
            NAME core99_t07_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t07.py
                --binary $<TARGET_FILE:core99_t07_hpc>
        )
    endif()
    if(CORE99_ENABLE_T21)
        add_executable(core99_t21_test tests/pollini_t21_test.cpp)
        target_link_libraries(core99_t21_test PRIVATE core99_t21_core)
        add_test(NAME core99_t21_cpp COMMAND core99_t21_test)
        add_test(
            NAME core99_t21_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t21.py
                --binary $<TARGET_FILE:core99_t21_hpc>
        )
        set_tests_properties(core99_t21_h5 PROPERTIES TIMEOUT 300)
    endif()
    if(CORE99_ENABLE_T72)
        add_executable(core99_t72_test tests/sorkhabi_t72_test.cpp)
        target_link_libraries(core99_t72_test PRIVATE core99_t72_core)
        add_test(NAME core99_t72_cpp COMMAND core99_t72_test)
        add_test(
            NAME core99_t72_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t72.py
                --binary $<TARGET_FILE:core99_t72_hpc>
        )
        set_tests_properties(core99_t72_h5 PROPERTIES TIMEOUT 300)
    endif()
    if(CORE99_ENABLE_T64)
        add_executable(core99_t64_test tests/sorkhabi_t64_test.cpp)
        target_link_libraries(core99_t64_test PRIVATE core99_t64_core)
        add_test(NAME core99_t64_cpp COMMAND core99_t64_test)
        add_test(
            NAME core99_t64_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t64.py
                --binary $<TARGET_FILE:core99_t64_hpc>
        )
        set_tests_properties(core99_t64_h5 PROPERTIES TIMEOUT 300)
    endif()
    if(CORE99_ENABLE_T67)
        add_executable(core99_t67_test tests/abdulrahman_t67_test.cpp)
        target_link_libraries(core99_t67_test PRIVATE core99_t67_core)
        add_test(NAME core99_t67_cpp COMMAND core99_t67_test)
        add_test(
            NAME core99_t67_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t67.py
                --binary $<TARGET_FILE:core99_t67_hpc>
        )
        set_tests_properties(core99_t67_h5 PROPERTIES TIMEOUT 300)
    endif()
    if(CORE99_ENABLE_T76)
        add_executable(core99_t76_test tests/sun_t76_test.cpp)
        target_link_libraries(core99_t76_test PRIVATE core99_t76_core)
        add_test(NAME core99_t76_cpp COMMAND core99_t76_test)
        add_test(
            NAME core99_t76_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t76.py
                --binary $<TARGET_FILE:core99_t76_hpc>
        )
        set_tests_properties(core99_t76_h5 PROPERTIES TIMEOUT 300)
    endif()
    if(CORE99_ENABLE_T81)
        add_executable(core99_t81_test tests/ti_t81_test.cpp)
        target_link_libraries(core99_t81_test PRIVATE core99_t81_core)
        add_test(NAME core99_t81_cpp COMMAND core99_t81_test)
        add_test(
            NAME core99_t81_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t81.py
                --binary $<TARGET_FILE:core99_t81_hpc>
        )
        set_tests_properties(core99_t81_h5 PROPERTIES TIMEOUT 300)
    endif()
    if(CORE99_ENABLE_T68)
        add_executable(core99_t68_test tests/hou_t68_test.cpp)
        target_link_libraries(core99_t68_test PRIVATE core99_t68_core)
        add_test(NAME core99_t68_cpp COMMAND core99_t68_test)
        add_test(
            NAME core99_t68_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t68.py
                --binary $<TARGET_FILE:core99_t68_hpc>
        )
        set_tests_properties(core99_t68_h5 PROPERTIES TIMEOUT 300)
    endif()
    if(CORE99_ENABLE_T78)
        add_executable(core99_t78_test tests/wu_t78_test.cpp)
        target_link_libraries(core99_t78_test PRIVATE core99_t78_core)
        add_test(NAME core99_t78_cpp COMMAND core99_t78_test)
        add_test(
            NAME core99_t78_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t78.py
                --binary $<TARGET_FILE:core99_t78_hpc>
        )
        set_tests_properties(core99_t78_h5 PROPERTIES TIMEOUT 600)
    endif()
    if(CORE99_ENABLE_T83)
        add_executable(core99_t83_test tests/cazzaro_t83_test.cpp)
        target_link_libraries(core99_t83_test PRIVATE core99_t83_core)
        target_compile_definitions(
            core99_t83_test PRIVATE CORE99_T31_DATA_ROOT="${CORE99_T31_DATA_ROOT}"
        )
        add_test(NAME core99_t83_cpp COMMAND core99_t83_test)
        add_test(
            NAME core99_t83_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t83.py
                --binary $<TARGET_FILE:core99_t83_hpc>
                --data-root ${CORE99_T31_DATA_ROOT}
        )
        set_tests_properties(core99_t83_cpp core99_t83_h5 PROPERTIES TIMEOUT 1800)
    endif()
    if(CORE99_ENABLE_T60)
        add_executable(core99_t60_test tests/feng_t60_test.cpp)
        target_link_libraries(core99_t60_test PRIVATE core99_t60_core)
        add_test(NAME core99_t60_cpp COMMAND core99_t60_test)
        add_test(
            NAME core99_t60_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t60.py
                --binary $<TARGET_FILE:core99_t60_hpc>
        )
        set_tests_properties(core99_t60_h5 PROPERTIES TIMEOUT 300)
    endif()
    if(CORE99_ENABLE_T69)
        add_executable(core99_t69_test tests/feng_t69_test.cpp)
        target_link_libraries(core99_t69_test PRIVATE core99_t69_core)
        add_test(NAME core99_t69_cpp COMMAND core99_t69_test)
        add_test(
            NAME core99_t69_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t69.py
                --binary $<TARGET_FILE:core99_t69_hpc>
        )
        set_tests_properties(core99_t69_cpp core99_t69_h5 PROPERTIES TIMEOUT 600)
    endif()
    if(CORE99_ENABLE_L0079)
        add_executable(core99_l0079_test tests/pillai_l0079_test.cpp)
        target_link_libraries(core99_l0079_test PRIVATE core99_l0079_core)
        add_test(NAME core99_l0079_cpp COMMAND core99_l0079_test)
        add_test(
            NAME core99_l0079_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_l0079.py
                --binary $<TARGET_FILE:core99_l0079_hpc>
        )
        set_tests_properties(
            core99_l0079_cpp core99_l0079_h5 PROPERTIES TIMEOUT 900
        )
    endif()
    if(CORE99_ENABLE_Y09)
        add_executable(core99_y09_test tests/li_y09_test.cpp)
        target_link_libraries(core99_y09_test PRIVATE core99_y09_core)
        add_test(NAME core99_y09_cpp COMMAND core99_y09_test)
        add_test(
            NAME core99_y09_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_y09.py
                --binary $<TARGET_FILE:core99_y09_hpc>
        )
        set_tests_properties(
            core99_y09_cpp core99_y09_h5 PROPERTIES TIMEOUT 900
        )
    endif()
    if(CORE99_ENABLE_Y14)
        add_executable(core99_y14_test tests/zhang_y14_test.cpp)
        target_link_libraries(core99_y14_test PRIVATE core99_y14_core)
        add_test(NAME core99_y14_cpp COMMAND core99_y14_test)
        add_test(
            NAME core99_y14_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_y14.py
                --binary $<TARGET_FILE:core99_y14_hpc>
        )
        set_tests_properties(
            core99_y14_cpp core99_y14_h5 PROPERTIES TIMEOUT 900
        )
    endif()
    if(CORE99_ENABLE_T11)
        add_executable(core99_t11_test tests/blockcopy_t11_test.cpp)
        target_link_libraries(core99_t11_test PRIVATE core99_t11_core)
        add_test(NAME core99_t11_cpp COMMAND core99_t11_test)
        add_test(
            NAME core99_t11_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t11.py
                --binary $<TARGET_FILE:core99_t11_hpc>
        )
        set_tests_properties(core99_t11_h5 PROPERTIES TIMEOUT 300)
    endif()
    if(CORE99_ENABLE_L0259)
        add_executable(core99_l0259_test tests/ju_l0259_test.cpp)
        target_link_libraries(core99_l0259_test PRIVATE core99_l0259_core)
        add_test(NAME core99_l0259_cpp COMMAND core99_l0259_test)
        add_test(
            NAME core99_l0259_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_l0259.py
                --binary $<TARGET_FILE:core99_l0259_hpc>
        )
        set_tests_properties(core99_l0259_h5 PROPERTIES TIMEOUT 600)
    endif()
    if(CORE99_ENABLE_T82)
        add_executable(core99_t82_test tests/cao_t82_test.cpp)
        target_link_libraries(core99_t82_test PRIVATE core99_t82_core)
        add_test(NAME core99_t82_cpp COMMAND core99_t82_test)
        add_test(
            NAME core99_t82_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t82.py
                --binary $<TARGET_FILE:core99_t82_hpc>
        )
        set_tests_properties(core99_t82_h5 PROPERTIES TIMEOUT 600)
    endif()
    if(CORE99_ENABLE_T85)
        add_executable(core99_t85_test tests/song_t85_test.cpp)
        target_link_libraries(core99_t85_test PRIVATE core99_t85_core)
        add_test(NAME core99_t85_cpp COMMAND core99_t85_test)
        add_test(
            NAME core99_t85_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t85.py
                --binary $<TARGET_FILE:core99_t85_hpc>
        )
        set_tests_properties(core99_t85_h5 PROPERTIES TIMEOUT 900)
    endif()
    if(CORE99_ENABLE_T24)
        add_executable(core99_t24_test tests/kim_t24_test.cpp)
        target_link_libraries(core99_t24_test PRIVATE core99_t24_core)
        add_test(NAME core99_t24_cpp COMMAND core99_t24_test)
        add_test(
            NAME core99_t24_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t24.py
                --binary $<TARGET_FILE:core99_t24_hpc>
        )
        set_tests_properties(core99_t24_h5 PROPERTIES TIMEOUT 1800)
    endif()
    add_executable(core99_t77_test tests/long_t77_test.cpp)
    target_link_libraries(core99_t77_test PRIVATE core99_t77_core)
    add_test(NAME core99_t77_cpp COMMAND core99_t77_test)
    add_test(
        NAME core99_t77_h5
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t77.py
            --binary $<TARGET_FILE:core99_t77_hpc>
    )
    if(CORE99_ENABLE_T31)
        add_executable(core99_t31_test tests/cazzaro_t31_test.cpp)
        target_link_libraries(core99_t31_test PRIVATE core99_t31_core)
        add_test(
            NAME core99_t31_cpp
            COMMAND core99_t31_test ${CORE99_T31_DATA_ROOT}
        )
        add_test(
            NAME core99_t31_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t31.py
                --binary $<TARGET_FILE:core99_t31_hpc>
        )
    endif()
    if(CORE99_ENABLE_T33)
        add_executable(core99_t33_test tests/cazzaro_t33_test.cpp)
        target_link_libraries(core99_t33_test PRIVATE core99_t33_core)
        add_test(
            NAME core99_t33_cpp
            COMMAND core99_t33_test ${CORE99_T31_DATA_ROOT}
        )
        add_test(
            NAME core99_t33_h5
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate_core99_t33.py
                --binary $<TARGET_FILE:core99_t33_hpc>
                --data-root ${CORE99_T31_DATA_ROOT}
        )
        set_tests_properties(core99_t33_h5 PROPERTIES TIMEOUT 1800)
    endif()
endif()
