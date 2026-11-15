# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.25)
project(kirimaspice VERSION 0.0.0 LANGUAGES CXX C)

# kirimaspice is built as C++23. 26 is accepted too, to see when it can move.
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
elseif(NOT CMAKE_CXX_STANDARD MATCHES "^(23|26)$")
    message(FATAL_ERROR "kirimaspice is built as C++23 (CMAKE_CXX_STANDARD 23, "
                        "or 26 to try the next standard), not ${CMAKE_CXX_STANDARD}")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
option(ENABLE_COVERAGE "Enable coverage" OFF)
option(SANITIZE_ADDRESS "Enable AddressSanitizer" OFF)

# Solver backends (docs: role separation)
option(KIRIMASPICE_WITH_SUPERLUDIST "Build with SuperLU_DIST backend" OFF)
option(KIRIMASPICE_WITH_SUNDIALS "Build with SUNDIALS adapters" OFF)
set(KIRIMASPICE_SOLVER "KLU" CACHE STRING "Default solver backend (KLU|SUPERLU_DIST|KLU_SUN)")
set_property(CACHE KIRIMASPICE_SOLVER PROPERTY STRINGS KLU SUPERLU_DIST KLU_SUN)

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
    GIT_TAG 1be298e1bd68957e4cd352e1f676f00e07dcfb57 # 12.2.0
)
set(FMT_MODULE OFF CACHE BOOL "Build fmt as a header and library, not a C++ module" FORCE)
FetchContent_MakeAvailable(fmt)

#==============================================================================
# Dependency: spdlog
#==============================================================================
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG 79524ddd08a4ec981b7fea76afd08ee05f83755d # v1.17.0
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
    GIT_TAG cf639f7f4ee125f68e1ccfba8d99ebc0de57b9fe # 3.2.7
)
FetchContent_MakeAvailable(pegtl)

#==============================================================================
# Dependency: ExprTk (header-only)
#==============================================================================
FetchContent_Declare(
    exprtk
    GIT_REPOSITORY https://github.com/ArashPartow/exprtk.git
    GIT_TAG 1e4a80b5ec9b4832ed59c6faa65f625a01b18ef0 # master as of 2026-11-13
)
FetchContent_MakeAvailable(exprtk)

#==============================================================================
# Dependency: nlohmann_json (header-only)
#==============================================================================
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03 # v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

#==============================================================================
# Dependency: Boost (Graph only, header-only subset)
#==============================================================================
FetchContent_Declare(
    Boost
    # Official release archive, pinned by its SHA-256 (also published next to it as
    # boost-1.86.0-cmake.tar.xz.txt). A git clone at a commit SHA cannot be shallow
    # and takes about 2.1 GB with submodules; the archive is 95 MB.
    URL https://github.com/boostorg/boost/releases/download/boost-1.86.0/boost-1.86.0-cmake.tar.xz
    URL_HASH SHA256=2c5ec5edcdff47ff55e27ed9560b0a0b94b07bd07ed9928b476150e16b0efc57
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)
set(BOOST_INCLUDE_LIBRARIES graph CACHE STRING "" FORCE)
FetchContent_MakeAvailable(Boost)

#==============================================================================
# Dependency: zstd
#==============================================================================
FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG 794ea1b0afca0f020f4e57b6732332231fb23c70 # v1.5.6
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
# NLopt: nonlinear optimization (COBYLA, BOBYQA, SLSQP and Nelder-Mead are used).
# NLOPT_LUKSAN=OFF leaves out the Luksan solvers, the only LGPL code in NLopt;
# what remains is MIT, except SLSQP, which is BSD-3-Clause (see NLopt's COPYING).
FetchContent_Declare(
    nlopt
    GIT_REPOSITORY https://github.com/stevengj/nlopt.git
    GIT_TAG 88c424d4f458412787df96fcc95218acbca224fd # v2.11.0
)
# NLopt build options
set(NLOPT_CXX ON CACHE BOOL "Build C++ interface" FORCE)
set(NLOPT_PYTHON OFF CACHE BOOL "Disable Python bindings" FORCE)
set(NLOPT_OCTAVE OFF CACHE BOOL "Disable Octave bindings" FORCE)
set(NLOPT_MATLAB OFF CACHE BOOL "Disable MATLAB bindings" FORCE)
set(NLOPT_GUILE OFF CACHE BOOL "Disable Guile bindings" FORCE)
set(NLOPT_SWIG OFF CACHE BOOL "Disable SWIG bindings" FORCE)
set(NLOPT_TESTS OFF CACHE BOOL "Disable NLopt tests" FORCE)
set(NLOPT_LUKSAN OFF CACHE BOOL "Build without the LGPL Luksan solvers" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static library" FORCE)
FetchContent_MakeAvailable(nlopt)
message(STATUS "NLopt will be built (static library, NLOPT_LUKSAN=OFF)")

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
        GIT_TAG f8d7d77c06936315286eb55f8de22cd23c188571 # v1.14.0
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
        GIT_TAG 344117638c8ff7e239044fd0fa7085839fc03021 # v1.8.3
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

    # Parser
    src/parser/netlist_parser.cpp

    # Circuit
    src/circuit/circuit_graph.cpp
    src/circuit/subcircuit.cpp  # Subcircuit support

    # Devices
    src/devices/resistor.cpp
    src/devices/capacitor.cpp
    src/devices/inductor.cpp
    src/devices/voltage_source.cpp
    src/devices/current_source.cpp
    src/devices/behavioral_source.cpp
    src/devices/diode.cpp
    src/devices/bjt.cpp         # BJT: one Gummel-Poon device for NPN and PNP
    src/devices/bjt_params.cpp  # BJT parameters from a card
    src/devices/mosfet_level1.cpp    # Unified Level 1 template (MOSFET_N/P)
    src/devices/mosfet_level3.cpp    # Unified Level 3 template (MOSFET_Level3_N/P)
    src/devices/mosfet_bsim1.cpp    # Unified BSIM1 template (MOSFET_BSIM1_N/P)
    src/devices/bsim3_core.cpp      # Shared BSIM3 DC core (Ids/Vth/Vdsat)
    src/devices/mosfet_bsim3.cpp    # Unified BSIM3 template (MOSFET_BSIM3_N/P)
    src/devices/mosfet_bsim4.cpp    # Unified BSIM4 template (MOSFET_BSIM4_N/P)
    src/devices/bsim4_core.cpp      # Shared BSIM4 evaluator
    src/devices/mosfet_bsim4_params.cpp  # BSIM4 parameter initialization
    src/devices/mosfet_sic.cpp      # Unified SiC MOSFET template
    src/devices/finfet.cpp          # Unified FinFET template
    src/devices/triac.cpp           # TRIAC (Triode for Alternating Current)
    # JFET, GaN HEMT, VDMOS, IGBT, SCR and MESFET are header-only templates
    # (jfet.hpp, gan_hemt.hpp, vdmos.hpp, igbt.hpp, scr.hpp, mesfet.hpp).

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

    # Solvers
    src/solvers/klu_solver.cpp
    src/solvers/solver_factory.cpp
    src/solvers/superlu_dist_solver.cpp
    src/solvers/dc_solver.cpp
    src/solvers/convergence_checker.cpp
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
    src/solvers/temperature_utils.cpp

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


# Backend compile definitions (visible to public headers)
target_compile_definitions(kirimaspice_lib
    PUBLIC
        KIRIMASPICE_VERSION="${PROJECT_VERSION}"
        KIRIMASPICE_BUILD_DEFAULT_SOLVER="${KIRIMASPICE_SOLVER}"
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
