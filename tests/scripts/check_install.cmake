# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# cmake --install installs the program alone (CMakeLists.txt): no library,
# no headers, and nothing of the fetched dependencies, whose install rules
# kirimaspice_build_only leaves out.
# Usage: cmake -DBUILD_DIR=<build> -DPREFIX=<empty directory> -P check_install.cmake

file(REMOVE_RECURSE "${PREFIX}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${PREFIX}"
                RESULT_VARIABLE result OUTPUT_QUIET)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "cmake --install failed: ${result}")
endif()
file(GLOB_RECURSE installed RELATIVE "${PREFIX}" "${PREFIX}/*")
if(NOT installed STREQUAL "bin/kirimaspice")
    message(FATAL_ERROR "cmake --install installed: ${installed}")
endif()
