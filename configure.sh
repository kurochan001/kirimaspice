#!/bin/bash
# Two-stage configuration script for kirimaspice
# Stage 1: Build SuiteSparse
# Stage 2: Configure main project with SUNDIALS + KLU

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
SUITESPARSE_BUILD_DIR="${BUILD_DIR}/suitesparse-external"
# Extra CMake args for the main project (stage 2)
MAIN_CMAKE_ARGS=("$@")

echo "=== Stage 1: Building SuiteSparse ==="

# Clean and create SuiteSparse build directory
rm -rf "${SUITESPARSE_BUILD_DIR}"
mkdir -p "${SUITESPARSE_BUILD_DIR}"

# Clone SuiteSparse
cd "${SUITESPARSE_BUILD_DIR}"
if [ ! -d "SuiteSparse" ]; then
    echo "Cloning SuiteSparse..."
    git clone --depth 1 --branch v7.8.0 https://github.com/DrTimothyAldenDavis/SuiteSparse.git
fi

# Configure and build SuiteSparse
cd SuiteSparse
mkdir -p build
cd build

echo "Configuring SuiteSparse..."
cmake .. \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_STATIC_LIBS=ON \
    -DSUITESPARSE_ENABLE_PROJECTS="suitesparse_config;amd;colamd;btf;cholmod;klu" \
    -DSUITESPARSE_USE_CUDA=OFF \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

echo "Building SuiteSparse (KLU and dependencies)..."
cmake --build . --target KLU_static --target AMD_static --target COLAMD_static --target BTF_static --target SuiteSparseConfig_static -j32

echo "SuiteSparse build complete!"
echo "Libraries: $(pwd)/lib"

# Export paths for stage 2
export SUITESPARSE_SRC="${SUITESPARSE_BUILD_DIR}/SuiteSparse"
export SUITESPARSE_BUILD="${SUITESPARSE_BUILD_DIR}/SuiteSparse/build"

echo ""
echo "=== Stage 2: Configuring kirimaspice with SUNDIALS + KLU ==="

cd "${SCRIPT_DIR}"
rm -rf "${BUILD_DIR}/CMakeCache.txt" "${BUILD_DIR}/CMakeFiles"

echo "Configuring main project..."
cmake -B "${BUILD_DIR}" -S . \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DKLU_INCLUDE_DIR="${SUITESPARSE_SRC}/KLU/Include" \
    -DAMD_INCLUDE_DIR="${SUITESPARSE_SRC}/AMD/Include" \
    -DCOLAMD_INCLUDE_DIR="${SUITESPARSE_SRC}/COLAMD/Include" \
    -DBTF_INCLUDE_DIR="${SUITESPARSE_SRC}/BTF/Include" \
    -DSUITESPARSECONFIG_INCLUDE_DIR="${SUITESPARSE_SRC}/SuiteSparse_config" \
    -DKLU_LIBRARY="${SUITESPARSE_BUILD}/KLU/libklu.a" \
    -DAMD_LIBRARY="${SUITESPARSE_BUILD}/AMD/libamd.a" \
    -DCOLAMD_LIBRARY="${SUITESPARSE_BUILD}/COLAMD/libcolamd.a" \
    -DBTF_LIBRARY="${SUITESPARSE_BUILD}/BTF/libbtf.a" \
    -DSUITESPARSECONFIG_LIBRARY="${SUITESPARSE_BUILD}/SuiteSparse_config/libsuitesparseconfig.a" \
    -DKLU_LIBRARIES="${SUITESPARSE_BUILD}/KLU/libklu.a;${SUITESPARSE_BUILD}/AMD/libamd.a;${SUITESPARSE_BUILD}/COLAMD/libcolamd.a;${SUITESPARSE_BUILD}/BTF/libbtf.a;${SUITESPARSE_BUILD}/SuiteSparse_config/libsuitesparseconfig.a" \
    "${MAIN_CMAKE_ARGS[@]}"

echo ""
echo "=== Configuration complete! ==="
echo "To build: cmake --build ${BUILD_DIR} -j32"
echo "To test:  cd ${BUILD_DIR} && ctest --output-on-failure -j32"
