# CMakeLists.txt
#
# Copyright 2025 Anivice Ives
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.30)

if ("X${CMAKE_BUILD_TYPE}" STREQUAL "XDebug")
    add_subdirectory(src/imgInTerm/)
endif ()

project(cfs CXX C)

find_program(CCACHE_PROGRAM ccache)
if (NOT "${CCACHE_PROGRAM}" STREQUAL "CCACHE_PROGRAM-NOTFOUND")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Using ${CCACHE_PROGRAM}")
endif ()

if ("X${CMAKE_BUILD_TYPE}" STREQUAL "XDebug")
    add_compile_definitions(DEBUG=1)
    set(compiler_options
            # Warnings and diagnostics
            -Wall                                   # Enable common warnings
            -Wextra                                 # Enable extra warnings
            -Wpedantic                              # Strict compliance with the standard
            -Wunused                                # Warn about unused variables, functions, etc.
            -Wuninitialized                         # Warn if variables are used uninitialized
            -fdiagnostics-show-option               # Show which option triggered the warning
            -fdiagnostics-color=always              # Enable colored diagnostics for better readability
            -march=native
            -mtune=native

            # Debugging and stack protection
            -g3                                     # Maximum debug information, including macro expansions
            -O0
            -fstack-usage                           # Generate stack usage info for each function
            -fstack-protector-all                   # Protect all functions with a stack canary to prevent stack overflow attacks
            # -D_FORTIFY_SOURCE=2                   # Buffer overflow detection on safer libc functions (e.g., memcpy).
            # You need to enable optimization for _FORTIFY_SOURCE to work!
            -gdwarf-4                               # Generate DWARF version 4 debug information

            -fno-eliminate-unused-debug-types
            -fno-omit-frame-pointer
            -Wl,-z,relro -Wl,-z,now
            -fstack-protector-all
    )

    set(linker_options
            # Linker options for memory safety, thread safety, and verbose debugging
            -Wl,--no-omagic                         # Prevent the generation of object files in memory; useful for debugging
            -Wl,--as-needed                         # Only link libraries that are actually needed to reduce binary size
            -Wl,--fatal-warnings                    # Treat all linker warnings as errors to catch issues early
            -Wl,-z,relro                            # Read-only relocations to prevent certain memory exploits (optional)
            -Wl,-z,now                              # Fully resolve all symbols during the link time for extra safety
            -Wl,-z,noexecstack                      # Prevent execution of code on the stack (security hardening)
            -Wl,-z,defs                             # Ensure all symbols are defined, and prevent undefined symbols
            -Wl,-O0

            -gdwarf-4                               # Generate detailed debug information for the linker
            -fno-eliminate-unused-debug-types
            -fno-omit-frame-pointer

            # Stack protection
            -fstack-protector-all                   # Link with stack protection for all functions
            -Wl,-z,relro -Wl,-z,now
    )

    list(APPEND compiler_options
            -fsanitize=address
            -fsanitize=undefined
    )

    list(APPEND linker_options
            -fsanitize=address
            -fsanitize=undefined
            -lasan
            -lubsan
    )

#    list(APPEND compiler_options
#            -fsanitize=thread
#            -fsanitize=undefined
#    )
#
#    list(APPEND linker_options
#            -fsanitize=thread
#            -fsanitize=undefined
#            -ltsan -lubsan
#    )

    add_compile_options(${compiler_options})
    add_link_options(${linker_options})
else ()
    set(optimization_flags
            -O3
            -march=native
            -mtune=native
            -flto
            -fomit-frame-pointer
            -ffast-math
            -fstrict-aliasing
            -fdata-sections
            -ffunction-sections
            -D_FORTIFY_SOURCE=2
            -fstack-protector-strong
            -Wl,-z,relro -Wl,-z,now
            -s
    )

    set(optimization_link_flags
            -O3
            -march=native
            -mtune=native
            -flto
            -fomit-frame-pointer
            -ffast-math
            -fstrict-aliasing
            -fdata-sections
            -ffunction-sections
            -Wl,--gc-sections
            -D_FORTIFY_SOURCE=2
            -fstack-protector-strong
            -Wl,-z,relro -Wl,-z,now
            -s
    )

    add_compile_definitions(DEBUG=0)
    add_compile_options(${optimization_flags})
    add_link_options(${optimization_link_flags})
endif ()

set(CMAKE_CXX_STANDARD 23)
include_directories(src/include)
add_compile_definitions(CORE_VERSION="0.0.1")
add_compile_definitions(BACKEND_VERSION="0.0.1")
add_library(core SHARED
        src/include/helper/cpp_assert.h
        src/include/helper/err_type.h
        src/helper/log.cpp              src/include/helper/log.h
        src/helper/backtrace.cpp        src/include/helper/backtrace.h
        src/helper/execute.cpp          src/include/helper/execute.h
        src/helper/color.cpp            src/include/helper/color.h
        src/helper/get_env.cpp          src/include/helper/get_env.h
        src/helper/arg_parser.cpp       src/include/helper/arg_parser.h
        src/helper/lz4.c                src/include/helper/lz4.h
        src/core/inode.cpp              src/include/core/inode.h
        src/core/crc64sum.cpp           src/include/core/crc64sum.h
        src/core/basic_io.cpp           src/include/core/basic_io.h
        src/core/cfs.cpp                src/include/core/cfs.h
        src/core/bitmap.cpp             src/include/core/bitmap.h
        src/core/block_io.cpp           src/include/core/block_io.h
        src/core/ring_buffer.cpp        src/include/core/ring_buffer.h
        src/core/blk_manager.cpp        src/include/core/blk_manager.h
        src/core/journal.cpp            src/include/core/journal.h
        src/core/block_attr.cpp         src/include/core/block_attr.h
)

if ("X${CMAKE_BUILD_TYPE}" STREQUAL "XDebug")
    add_executable(test.exe src/utest/test.cpp src/include/test/test.h src/utest/main.cpp)
    target_link_libraries(test.exe PRIVATE core tiv)
    target_compile_definitions(test.exe PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    add_custom_target(test
            COMMAND ${CMAKE_BINARY_DIR}/test.exe
            USES_TERMINAL
            DEPENDS test.exe
    )
endif ()

add_executable(cfs src/utils/cfs.cpp src/utils/mkfs.cpp src/utils/fsck.cpp src/utils/mount.cpp
        src/utils/journal_hd.cpp src/utils/journal_hd.h)
target_link_libraries(cfs PRIVATE core)
add_custom_target(utils ALL
        COMMAND ${CMAKE_COMMAND} -E create_symlink cfs mount.cfs
        COMMAND ${CMAKE_COMMAND} -E create_symlink cfs fsck.cfs
        COMMAND ${CMAKE_COMMAND} -E create_symlink cfs mkfs.cfs
        COMMENT "Creating CFS utilities..."
        DEPENDS cfs
)

add_executable(pro src/main.cpp)
target_link_libraries(pro PRIVATE core)
