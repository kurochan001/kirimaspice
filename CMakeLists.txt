cmake_minimum_required(VERSION 3.25)
project(kirimaspice VERSION 0.31.1 LANGUAGES CXX C)

# C++20 standard required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
option(ENABLE_COVERAGE "Enable coverage" OFF)
option(SANITIZE_ADDRESS "Enable AddressSanitizer" OFF)

# Solver backends (docs: role separation)
option(KIRIMASPICE_WITH_SUPERLUMT "Build with SuperLU_MT backend" ON)
option(KIRIMASPICE_BUILD_SUPERLUMT "Build SuperLU_MT from source (FetchContent)" OFF)
option(KIRIMASPICE_WITH_SUPERLUDIST "Build with SuperLU_DIST backend" OFF)
option(KIRIMASPICE_WITH_SUNDIALS "Build with SUNDIALS adapters" OFF)
set(KIRIMASPICE_SOLVER "KLU" CACHE STRING "Default solver backend (KLU|SUPERLU_MT|SUPERLU_DIST|KLU_SUN)")
set_property(CACHE KIRIMASPICE_SOLVER PROPERTY STRINGS KLU SUPERLU_MT SUPERLU_DIST KLU_SUN)
option(BUILD_NGSPICE_BSIM4_REF "Build vendored ngspice BSIM4 reference library" OFF)
set(NGSPICE_SRC_ROOT "${CMAKE_SOURCE_DIR}/../ngspice-d93a11_pre-master-46/src" CACHE PATH "Path to ngspice source tree (root of src/)")
option(USE_NGSPICE_BSIM4_REF "Use ngspice BSIM4 reference implementation (adapter build only)" OFF)

# Coverage flags
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(--coverage -O0 -g)
        add_link_options(--coverage)
    endif()
endif()

# AddressSanitizer (for debugging runtime issues)
if(SANITIZE_ADDRESS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address -fno-omit-frame-pointer)
        message(STATUS "AddressSanitizer enabled")
    endif()
endif()

# FetchContent module
include(FetchContent)

# Find OpenMP (optional, improves SuiteSparse performance)
find_package(OpenMP)
if(OpenMP_FOUND)
    message(STATUS "OpenMP found - enabling parallel support")
else()
    message(WARNING "OpenMP not found - building without parallel support")
endif()

# Workaround for Clang: manually set OpenMP flags if not detected
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT OpenMP_CXX_FOUND)
    set(OpenMP_CXX_FLAGS "-fopenmp=libomp")
    set(OpenMP_CXX_LIB_NAMES "omp")
    set(OpenMP_omp_LIBRARY "/usr/lib/llvm-18/lib/libomp.so")
    set(OpenMP_CXX_FOUND TRUE)
    message(STATUS "Manually set OpenMP flags for Clang++")
endif()

# Disable examples and tests for dependencies
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

#==============================================================================
# Dependency: fmt
#==============================================================================
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 9.1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(fmt)

#==============================================================================
# Dependency: spdlog
#==============================================================================
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
    GIT_SHALLOW TRUE
)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
set(SPDLOG_COMPILED_LIB ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

#==============================================================================
# Dependency: PEGTL
#==============================================================================
FetchContent_Declare(
    pegtl
    GIT_REPOSITORY https://github.com/taocpp/PEGTL.git
    GIT_TAG 3.2.7
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(pegtl)

#==============================================================================
# Dependency: ExprTk (header-only)
#==============================================================================
FetchContent_Declare(
    exprtk
    GIT_REPOSITORY https://github.com/ArashPartow/exprtk.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(exprtk)

#==============================================================================
# Dependency: nlohmann_json (header-only)
#==============================================================================
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

#==============================================================================
# Dependency: Boost (Graph only, header-only subset)
#==============================================================================
FetchContent_Declare(
    Boost
    GIT_REPOSITORY https://github.com/boostorg/boost.git
    GIT_TAG boost-1.86.0
    GIT_SHALLOW TRUE
)
set(BOOST_INCLUDE_LIBRARIES graph CACHE STRING "" FORCE)
FetchContent_MakeAvailable(Boost)

#==============================================================================
# Dependency: zstd
#==============================================================================
FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.6
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR build/cmake
)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(CMAKE_POLICY_DEFAULT_CMP0000 NEW CACHE STRING "" FORCE)
FetchContent_MakeAvailable(zstd)

#==============================================================================
# Dependency: NLopt (Nonlinear optimization library)
#==============================================================================
# NLopt: MIT/LGPL dual-licensed library for nonlinear optimization
# Used for Advanced circuit optimization (COBYLA, BOBYQA, SLSQP, Nelder-Mead)
# License: MIT (compatible with kirimaspice's MIT license)
FetchContent_Declare(
    nlopt
    GIT_REPOSITORY https://github.com/stevengj/nlopt.git
    GIT_TAG v2.8.0
    GIT_SHALLOW TRUE
)
# NLopt build options
set(NLOPT_CXX ON CACHE BOOL "Build C++ interface" FORCE)
set(NLOPT_PYTHON OFF CACHE BOOL "Disable Python bindings" FORCE)
set(NLOPT_OCTAVE OFF CACHE BOOL "Disable Octave bindings" FORCE)
set(NLOPT_MATLAB OFF CACHE BOOL "Disable MATLAB bindings" FORCE)
set(NLOPT_GUILE OFF CACHE BOOL "Disable Guile bindings" FORCE)
set(NLOPT_SWIG OFF CACHE BOOL "Disable SWIG bindings" FORCE)
set(NLOPT_TESTS OFF CACHE BOOL "Disable NLopt tests" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static library" FORCE)
FetchContent_MakeAvailable(nlopt)
message(STATUS "NLopt ${nlopt_VERSION} will be built (static library)")

#==============================================================================
# Dependency: SuiteSparse KLU (system library, linked dynamically)
#==============================================================================
# KLU and BTF are LGPL-2.1-or-later; AMD, COLAMD and SuiteSparse_config are
# BSD-3-Clause. They are not included in this repository. CMake looks for the
# SuiteSparse installed on the system and links it dynamically. To use another
# installation, add its prefix to CMAKE_PREFIX_PATH.
set(KIRIMASPICE_KLU_MIN_VERSION 2.3.2)  # KLU in SuiteSparse 7.6.1 (Ubuntu 24.04)

find_path(KLU_INCLUDE_DIR klu.h PATH_SUFFIXES suitesparse)
find_library(KLU_LIBRARY NAMES klu)
find_library(BTF_LIBRARY NAMES btf)
find_library(AMD_LIBRARY NAMES amd)
find_library(COLAMD_LIBRARY NAMES colamd)
find_library(SUITESPARSECONFIG_LIBRARY NAMES suitesparseconfig)

set(_kirimaspice_klu_missing "")
foreach(_var KLU_INCLUDE_DIR KLU_LIBRARY BTF_LIBRARY AMD_LIBRARY COLAMD_LIBRARY SUITESPARSECONFIG_LIBRARY)
    if(NOT ${_var})
        list(APPEND _kirimaspice_klu_missing ${_var})
    endif()
endforeach()
if(_kirimaspice_klu_missing)
    message(FATAL_ERROR
        "SuiteSparse (KLU) was not found: ${_kirimaspice_klu_missing}\n"
        "Install the SuiteSparse development files first, for example:\n"
        "  Ubuntu/Debian:     sudo apt install libsuitesparse-dev\n"
        "  macOS (Homebrew):  brew install suite-sparse\n"
        "or add the prefix of a SuiteSparse installation to CMAKE_PREFIX_PATH.")
endif()

file(STRINGS "${KLU_INCLUDE_DIR}/klu.h" _kirimaspice_klu_version_lines
     REGEX "^#define KLU_(MAIN|SUB|SUBSUB)_VERSION[ \t]+[0-9]+")
foreach(_part MAIN SUB SUBSUB)
    string(REGEX REPLACE ".*#define KLU_${_part}_VERSION[ \t]+([0-9]+).*" "\\1"
           _kirimaspice_klu_${_part} "${_kirimaspice_klu_version_lines}")
endforeach()
set(KLU_VERSION "${_kirimaspice_klu_MAIN}.${_kirimaspice_klu_SUB}.${_kirimaspice_klu_SUBSUB}")
if(KLU_VERSION VERSION_LESS KIRIMASPICE_KLU_MIN_VERSION)
    message(FATAL_ERROR
        "KLU ${KLU_VERSION} found in ${KLU_INCLUDE_DIR}; ${KIRIMASPICE_KLU_MIN_VERSION} or newer is required.")
endif()
message(STATUS "Found KLU ${KLU_VERSION}: ${KLU_LIBRARY}")

foreach(_pair "KLU;KLU" "AMD;AMD" "COLAMD;COLAMD" "BTF;BTF" "Config;SUITESPARSECONFIG")
    list(GET _pair 0 _target)
    list(GET _pair 1 _var)
    add_library(SuiteSparse::${_target} UNKNOWN IMPORTED)
    set_target_properties(SuiteSparse::${_target} PROPERTIES
        IMPORTED_LOCATION "${${_var}_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${KLU_INCLUDE_DIR}"
    )
endforeach()


#==============================================================================
# Dependency: Google Test (if testing enabled)
#==============================================================================
if(BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
        GIT_SHALLOW TRUE
    )
    # Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    include(GoogleTest)
endif()

#==============================================================================
# Dependency: Google Benchmark (if benchmarks enabled)
#==============================================================================
if(BUILD_BENCHMARKS)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
        GIT_SHALLOW TRUE
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    # GCC 14 + libstdc++ の警告で -Werror が落ちるのを回避
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    # それでも一部環境で -Werror が残ることがあるため、明示的に無効化
    if(TARGET benchmark)
        target_compile_options(benchmark PRIVATE -Wno-error -Wno-error=maybe-uninitialized -Wno-maybe-uninitialized)
    endif()
    if(TARGET benchmark_main)
        target_compile_options(benchmark_main PRIVATE -Wno-error -Wno-error=maybe-uninitialized -Wno-maybe-uninitialized)
    endif()
endif()

#==============================================================================
# Main library target
#==============================================================================
add_library(kirimaspice_lib STATIC
    # Core
    src/core/types.cpp
    src/core/matrix_types.cpp

    # Parser (Week 1-2)
    src/parser/netlist_parser.cpp

    # Circuit (Week 1-2)
    src/circuit/circuit_graph.cpp
    src/circuit/subcircuit.cpp  # Subcircuit support

    # Devices (Week 3)
    src/devices/resistor.cpp
    src/devices/capacitor.cpp
    src/devices/inductor.cpp
    src/devices/voltage_source.cpp
    src/devices/time_varying_voltage_source.cpp
    src/devices/current_source.cpp
    src/devices/behavioral_source.cpp
    src/devices/diode.cpp
    src/devices/bjt_base.cpp    # BJT common base class
    src/devices/bjt_npn.cpp
    src/devices/bjt_pnp.cpp
    # Step 5.1: mosfet_n.cpp and mosfet_p.cpp removed - now unified in mosfet_level1.cpp
    src/devices/mosfet_level1.cpp    # Unified Level 1 template (MOSFET_N/P)
    # Step 5.4: mosfet_level3_n.cpp and mosfet_level3_p.cpp removed - now unified in mosfet_level3.cpp
    src/devices/mosfet_level3.cpp    # Unified Level 3 template (MOSFET_Level3_N/P)
    # Step 4.1: jfet_n.cpp and jfet_p.cpp removed - now header-only template in jfet.hpp
    # Step 5.5: mosfet_bsim1_n.cpp and mosfet_bsim1_p.cpp removed - now unified in mosfet_bsim1.cpp
    src/devices/mosfet_bsim1.cpp    # Step 5.5: Unified BSIM1 template (MOSFET_BSIM1_N/P)
    src/devices/bsim3_core.cpp      # Shared BSIM3 DC core (Ids/Vth/Vdsat)
    # Step 5.6: mosfet_bsim3_n.cpp and mosfet_bsim3_p.cpp removed - now unified in mosfet_bsim3.cpp
    src/devices/mosfet_bsim3.cpp    # Step 5.6: Unified BSIM3 template (MOSFET_BSIM3_N/P)
    # Step 5.7: mosfet_bsim4_n.cpp and mosfet_bsim4_p.cpp removed - now unified in mosfet_bsim4.cpp
    src/devices/mosfet_bsim4.cpp    # Step 5.7: Unified BSIM4 template (MOSFET_BSIM4_N/P)
    src/devices/bsim4_core.cpp      # Shared BSIM4 evaluator
    src/devices/mosfet_bsim4_params.cpp  # BSIM4 parameter initialization
    src/devices/mosfet_sic.cpp      # Step 5.3: Unified SiC MOSFET template
    # Step 3.1: gan_hemt_n.cpp and gan_hemt_p.cpp removed - now header-only template in gan_hemt.hpp
    src/devices/finfet.cpp          # Step 5.2: Unified FinFET template
    # Step 3.2: vdmos_n.cpp and vdmos_p.cpp removed - now header-only template in vdmos.hpp
    # Step 3.3: igbt_n.cpp and igbt_p.cpp removed - now header-only template in igbt.hpp
    # Step 4.3: scr_n.cpp and scr_p.cpp removed - now header-only template in scr.hpp
    src/devices/triac.cpp           # TRIAC (Triode for Alternating Current)
    # Step 4.2: mesfet_n.cpp and mesfet_p.cpp removed - now header-only template in mesfet.hpp

    # Controlled Sources
    src/devices/vcvs.cpp
    src/devices/cccs.cpp
    src/devices/vccs.cpp
    src/devices/ccvs.cpp

    # Mutual Inductance
    src/devices/mutual_inductance.cpp

    # Transmission Line
    src/devices/tline_lossless.cpp
    src/devices/tline_lossy.cpp

    # Solvers (Week 3-5)
    src/solvers/klu_solver.cpp
    src/solvers/solver_factory.cpp
    src/solvers/superlu_mt_solver.cpp
    src/solvers/superlu_dist_solver.cpp
    src/solvers/dc_solver.cpp
    src/solvers/convergence_checker.cpp  # Step 2.1
    src/solvers/transient_solver.cpp
    src/solvers/ac_solver.cpp
    src/solvers/noise_solver.cpp
    src/solvers/parameter_sweep.cpp
    src/solvers/sensitivity_analyzer.cpp
    src/solvers/transfer_function_solver.cpp
    src/solvers/fourier_analyzer.cpp
    src/solvers/pole_zero_solver.cpp
    src/solvers/distortion_solver.cpp
    src/solvers/sparam_solver.cpp
    src/solvers/temperature_utils.cpp  # Step 2.4

    # Analysis
    src/analysis/random_distribution.cpp  # Monte Carlo distributions
    src/analysis/monte_carlo_executor.cpp  # Monte Carlo executor
    src/analysis/monte_carlo_gpu.cpp  # Monte Carlo GPU/Batch (PoC)
    src/analysis/wcase_analyzer.cpp  # Worst-case analyzer
    src/analysis/circuit_optimizer.cpp  # Circuit optimizer
    src/analysis/bsim4_parameter_extractor.cpp  # BSIM4 parameter extraction

    # Measure
    src/measure/measure.cpp

    # Integrators
    # src/integrators/backward_euler.cpp

    # Output
    src/output/ssf_writer.cpp
    src/output/ac_csv_writer.cpp
    src/output/raw_writer.cpp     # (Nutmeg ASCII)
    src/output/raw_binary_writer.cpp  # (Raw Binary)
    src/output/wav_writer.cpp     # (WAV)
    src/output/touchstone_writer.cpp
)

target_include_directories(kirimaspice_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${exprtk_SOURCE_DIR}
)

target_link_libraries(kirimaspice_lib
    PUBLIC
        spdlog::spdlog
        fmt::fmt
        taocpp::pegtl
        Boost::graph
        SuiteSparse::KLU
        SuiteSparse::AMD
        SuiteSparse::COLAMD
        SuiteSparse::BTF
        SuiteSparse::Config
        libzstd_static
        nlopt
)

## SuperLU_MT linking/defines are applied after detection block (later in file)

# Backend compile definitions (visible to public headers)
target_compile_definitions(kirimaspice_lib
    PUBLIC
        KIRIMASPICE_BUILD_DEFAULT_SOLVER="${KIRIMASPICE_SOLVER}"
        KIRIMASPICE_WITH_SUPERLUMT=$<BOOL:${KIRIMASPICE_WITH_SUPERLUMT}>
        KIRIMASPICE_WITH_SUPERLUDIST=$<BOOL:${KIRIMASPICE_WITH_SUPERLUDIST}>
        KIRIMASPICE_WITH_SUNDIALS=$<BOOL:${KIRIMASPICE_WITH_SUNDIALS}>
)

# Link OpenMP if found
if(OpenMP_FOUND)
    target_link_libraries(kirimaspice_lib PUBLIC OpenMP::OpenMP_CXX)
endif()

# Compiler warnings
target_compile_options(kirimaspice_lib PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wno-dangling-reference>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

#==============================================================================
# Main executable
#==============================================================================
add_executable(kirimaspice src/main.cpp)
target_link_libraries(kirimaspice PRIVATE kirimaspice_lib)

#==============================================================================
# Tests
#==============================================================================
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

#==============================================================================
# Installation
#==============================================================================
install(TARGETS kirimaspice_lib kirimaspice
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)

#==============================================================================
# Documentation (Doxygen)
#==============================================================================
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/docs)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_PRIVATE YES)
    set(DOXYGEN_RECURSIVE YES)

    doxygen_add_docs(docs
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        COMMENT "Generating API documentation with Doxygen"
    )
endif()
#==============================================================================
# Optional: SuperLU_MT (and serial SuperLU) detection for backend
#==============================================================================
if(KIRIMASPICE_WITH_SUPERLUMT)
    find_package(BLAS REQUIRED)
    find_package(LAPACK REQUIRED)

    if(KIRIMASPICE_BUILD_SUPERLUMT)
        # Build SuperLU_MT from source using FetchContent
        message(STATUS "Building SuperLU_MT from source (FetchContent)...")

        include(FetchContent)
        FetchContent_Declare(
            SuperLU_MT
            GIT_REPOSITORY https://github.com/xiaoyeli/superlu_mt.git
            GIT_TAG v4.0.1
            GIT_SHALLOW TRUE
        )

        # SuperLU_MT build options
        set(PLAT "_OPENMP" CACHE STRING "Threading flavor for SuperLU_MT" FORCE)
        set(enable_openmp ON CACHE BOOL "Enable OpenMP in SuperLU_MT" FORCE)
        set(enable_pthread OFF CACHE BOOL "Disable pthread in SuperLU_MT" FORCE)
        set(enable_examples OFF CACHE BOOL "Disable SuperLU_MT examples" FORCE)
        set(enable_tests OFF CACHE BOOL "Disable SuperLU_MT tests" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build SuperLU_MT as static library" FORCE)

        FetchContent_MakeAvailable(SuperLU_MT)

        # Create alias target for consistency (target name is superlu_mt_OPENMP)
        if(TARGET superlu_mt_OPENMP AND NOT TARGET SuperLU::MT)
            add_library(SuperLU::MT ALIAS superlu_mt_OPENMP)

            # Add SuperLU_MT include directories to kirimaspice_lib (PRIVATE to avoid conflicts with SuiteSparse)
            target_include_directories(kirimaspice_lib PRIVATE
                ${superlu_mt_SOURCE_DIR}/SRC
            )

            set(HAVE_SUPERLUMT TRUE)
            set(SUPERLUMT_FROM_SOURCE TRUE)
            message(STATUS "SuperLU_MT built from source with OpenMP support (target: superlu_mt_OPENMP)")
        elseif(TARGET superlu_mt_PTHREAD AND NOT TARGET SuperLU::MT)
            add_library(SuperLU::MT ALIAS superlu_mt_PTHREAD)

            # Add SuperLU_MT include directories to kirimaspice_lib (PRIVATE to avoid conflicts with SuiteSparse)
            target_include_directories(kirimaspice_lib PRIVATE
                ${superlu_mt_SOURCE_DIR}/SRC
            )

            set(HAVE_SUPERLUMT TRUE)
            set(SUPERLUMT_FROM_SOURCE TRUE)
            message(STATUS "SuperLU_MT built from source with pthread support (target: superlu_mt_PTHREAD)")
        else()
            set(HAVE_SUPERLUMT FALSE)
            message(WARNING "SuperLU_MT FetchContent failed - no valid target found")
        endif()
    else()
        # Use system-installed SuperLU_MT
        # Allow user to specify SUPERLU_ROOT
        if(DEFINED ENV{SUPERLU_ROOT} AND NOT DEFINED SUPERLU_ROOT)
            set(SUPERLU_ROOT $ENV{SUPERLU_ROOT})
        endif()

        find_path(SUPERLUMT_INCLUDE_DIR
            NAMES slu_ddefs.h superlu/slu_ddefs.h superlu_mt/superlumt_ddefs.h
            HINTS ${SUPERLU_ROOT}
            PATH_SUFFIXES include include/superlu include/superlu_mt
        )

        find_library(SUPERLUMT_LIBRARY
            NAMES superlu_mt superlu
            HINTS ${SUPERLU_ROOT}
            PATH_SUFFIXES lib lib64
        )

        if(SUPERLUMT_INCLUDE_DIR AND SUPERLUMT_LIBRARY AND BLAS_FOUND AND LAPACK_FOUND)
            add_library(SuperLU::MT UNKNOWN IMPORTED)
            set_target_properties(SuperLU::MT PROPERTIES
                IMPORTED_LOCATION "${SUPERLUMT_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${SUPERLUMT_INCLUDE_DIR}"
            )
            set(HAVE_SUPERLUMT TRUE)
            message(STATUS "SuperLU include: ${SUPERLUMT_INCLUDE_DIR}")
            message(STATUS "SuperLU/MT lib: ${SUPERLUMT_LIBRARY}")
        else()
            set(HAVE_SUPERLUMT FALSE)
            message(WARNING "SuperLU_MT not found (or BLAS/LAPACK missing) — backend will be disabled")
        endif()
    endif()
endif()

# Apply SuperLU_MT link + compile definitions now that detection finished
if(KIRIMASPICE_WITH_SUPERLUMT AND HAVE_SUPERLUMT)
    target_link_libraries(kirimaspice_lib PUBLIC SuperLU::MT ${BLAS_LIBRARIES} ${LAPACK_LIBRARIES})
    target_compile_definitions(kirimaspice_lib PUBLIC KIRIMASPICE_HAVE_SUPERLUMT=1)

    # Add compile definition if SuperLU_MT is built from source
    if(SUPERLUMT_FROM_SOURCE)
        target_compile_definitions(kirimaspice_lib PUBLIC KIRIMASPICE_SUPERLUMT_FROM_SOURCE=1)
    else()
        target_compile_definitions(kirimaspice_lib PUBLIC KIRIMASPICE_SUPERLUMT_FROM_SOURCE=0)
    endif()
else()
    target_compile_definitions(kirimaspice_lib PUBLIC KIRIMASPICE_HAVE_SUPERLUMT=0)
    target_compile_definitions(kirimaspice_lib PUBLIC KIRIMASPICE_SUPERLUMT_FROM_SOURCE=0)
endif()

#==============================================================================
# Optional: SuperLU_DIST detection (placeholder)
#==============================================================================
if(KIRIMASPICE_WITH_SUPERLUDIST)
    # Allow user to specify SUPERLUDIST_ROOT
    if(DEFINED ENV{SUPERLUDIST_ROOT} AND NOT DEFINED SUPERLUDIST_ROOT)
        set(SUPERLUDIST_ROOT $ENV{SUPERLUDIST_ROOT})
    endif()

    find_path(SUPERLUDIST_INCLUDE_DIR
        NAMES superlu_defs.h superlu_dist_config.h
        HINTS ${SUPERLUDIST_ROOT}
        PATH_SUFFIXES include include/superlu_dist
    )

    find_library(SUPERLUDIST_LIBRARY
        NAMES superlu_dist psuperlu_dist
        HINTS ${SUPERLUDIST_ROOT}
        PATH_SUFFIXES lib lib64
    )

    if(SUPERLUDIST_INCLUDE_DIR AND SUPERLUDIST_LIBRARY)
        add_library(SuperLU::DIST UNKNOWN IMPORTED)
        set_target_properties(SuperLU::DIST PROPERTIES
            IMPORTED_LOCATION "${SUPERLUDIST_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SUPERLUDIST_INCLUDE_DIR}"
        )
        message(STATUS "SuperLU_DIST include: ${SUPERLUDIST_INCLUDE_DIR}")
        message(STATUS "SuperLU_DIST lib: ${SUPERLUDIST_LIBRARY}")
    else()
        message(WARNING "SuperLU_DIST not found — backend remains stub-only")
    endif()
endif()

#==============================================================================
# Vendored ngspice BSIM4 (reference C implementation, not yet wired into core)
#==============================================================================
if(BUILD_NGSPICE_BSIM4_REF)
    file(GLOB NGSPICE_BSIM4_SOURCES external/ngspice_bsim4_ref/*.c)
    if(NGSPICE_BSIM4_SOURCES)
        add_library(ngspice_bsim4_ref STATIC ${NGSPICE_BSIM4_SOURCES})
        target_sources(ngspice_bsim4_ref PRIVATE
            ${CMAKE_SOURCE_DIR}/src/devices/bsim4_ngspice_stubs.cpp
        )
        target_include_directories(ngspice_bsim4_ref PUBLIC
            ${CMAKE_SOURCE_DIR}/external/ngspice_bsim4_ref
            ${NGSPICE_SRC_ROOT}/include
            ${NGSPICE_SRC_ROOT}/include/ngspice
        )
        set_target_properties(ngspice_bsim4_ref PROPERTIES
            C_STANDARD 99
            POSITION_INDEPENDENT_CODE ON
        )
        target_compile_options(ngspice_bsim4_ref PRIVATE -w)
    endif()
endif()

#------------------------------------------------------------------------------
# Optional: adapter that wraps ngspice BSIM4 into kirimaspice interface
#------------------------------------------------------------------------------
if(USE_NGSPICE_BSIM4_REF AND BUILD_NGSPICE_BSIM4_REF)
    target_sources(kirimaspice_lib PRIVATE
        src/devices/bsim4_ngspice_adapter.cpp
        src/devices/bsim4_ngspice_matrix_map.cpp
        src/devices/bsim4_ngspice_bridge_flag.cpp
    )
    target_link_libraries(kirimaspice_lib PRIVATE ngspice_bsim4_ref)
    target_compile_definitions(kirimaspice_lib PUBLIC KIRIMASPICE_USE_NGSPICE_BSIM4_REF=1)
else()
    target_sources(kirimaspice_lib PRIVATE
        src/devices/bsim4_ngspice_bridge_flag.cpp
    )
    target_compile_definitions(kirimaspice_lib PUBLIC KIRIMASPICE_USE_NGSPICE_BSIM4_REF=0)
endif()

# Debug executable for parameter extraction
if(EXISTS /tmp/debug_phase30_9_4.cpp)
    add_executable(debug_phase30_9_4 /tmp/debug_phase30_9_4.cpp)
    
    target_link_libraries(debug_phase30_9_4
        PRIVATE
            kirimaspice_lib
            spdlog::spdlog
            fmt::fmt
    )
    
    target_include_directories(debug_phase30_9_4
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
    )
    
    target_compile_features(debug_phase30_9_4 PRIVATE cxx_std_20)
endif()
