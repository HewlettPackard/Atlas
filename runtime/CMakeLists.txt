#
# (c) Copyright 2016 Hewlett Packard Enterprise Development LP
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. This program is
# distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
# for more details. You should have received a copy of the GNU Lesser
# General Public License along with this program. If not, see
# <http://www.gnu.org/licenses/>.
#
# Top level CMakeLists for Atlas project

cmake_minimum_required (VERSION 3.1.0)

project (Atlas)
set (Atlas_VERSION_MAJOR 1)
set (Atlas_VERSION_MINOR 0)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR AND NOT MSVC_IDE)
  message(FATAL_ERROR "In-source builds are not allowed.
Please create a directory and run cmake from there, passing the path
to this source directory as the last argument.
This process created the file `CMakeCache.txt' and the directory `CMakeFiles'.
Please delete them.")
endif ()

#need pthreads to build tests and tools - find an equivalent pthread compatible library on host
set (THREADS_PREFER_PTHREAD_FLAG ON)
find_package (Threads REQUIRED)
link_libraries (Threads::Threads)

#helper function used to build up relative file paths to files to be copied to build dir
function (prepend_paths FILES TARGET_PATH)
    set (LISTVAR "")
    foreach (t ${ARGN})
        list (APPEND LISTVAR "${TARGET_PATH}/${t}")
    endforeach (t)
    set (${FILES} "${LISTVAR}" PARENT_SCOPE)
endfunction (prepend_paths)

set (ATLAS_COPY_FILES ${ATLAS_COPY_FILES} README.md)

set (ATLAS_INTERNAL_INCLUDES "src/internal_includes")
set (ATLAS_INCLUDE_DIR "include")
set (ATLAS_INCLUDE_FILES atlas_alloc.h atlas_api.h atlas_alloc_cpp.hpp)
prepend_paths (ATLAS_INCLUDE_FILES ${ATLAS_INCLUDE_DIR} ${ATLAS_INCLUDE_FILES})
set (ATLAS_COPY_FILES ${ATLAS_COPY_FILES} ${ATLAS_INCLUDE_FILES})

set (ATLAS_TESTS_DIR "tests")
set (ATLAS_TESTS_FILES run_quick_test)
prepend_paths (ATLAS_TESTS_FILES ${ATLAS_TESTS_DIR} ${ATLAS_TESTS_FILES})
set (ATLAS_COPY_FILES ${ATLAS_COPY_FILES} ${ATLAS_TESTS_FILES})

set (ATLAS_TESTS_DATA_STRUCT_IN_DIR "${ATLAS_TESTS_DIR}/data_structures_inputs")
set (ATLAS_TESTS_DATA_STRUCT_IN_FILES timing.txt cow_array_list_nvm.ref cow_array_list.ref queue_nvm.ref queue.ref
                       sll_ll.ref sll_nvm.ref sll.ref alarm_clock.ref alarm_clock_nvm.ref cow_array_list.in
                       cow_array_list_nvm.in queue_nvm.in queue.in sll.in sll_ll.in sll_nvm.in alarm_clock.in
                       alarm_clock_nvm.in stores.ref stores_nvm.ref stores.in stores_nvm.in)
prepend_paths (ATLAS_TESTS_DATA_STRUCT_IN_FILES ${ATLAS_TESTS_DATA_STRUCT_IN_DIR} ${ATLAS_TESTS_DATA_STRUCT_IN_FILES})
set (ATLAS_COPY_FILES ${ATLAS_COPY_FILES} ${ATLAS_TESTS_DATA_STRUCT_IN_FILES})

set (ATLAS_CONSISTENCY_DIR "${ATLAS_TESTS_DIR}/consistency")
set (ATLAS_CONSISTENCY_TESTS test_consistency)
prepend_paths (ATLAS_CONSISTENCY_TESTS ${ATLAS_CONSISTENCY_DIR} ${ATLAS_CONSISTENCY_TESTS})
set (ATLAS_COPY_FILES ${ATLAS_COPY_FILES} ${ATLAS_CONSISTENCY_TESTS})

set (ATLAS_REGION_DIR "${ATLAS_TESTS_DIR}/region")
set (ATLAS_REGION_TESTS test_region)
prepend_paths (ATLAS_REGION_TESTS ${ATLAS_REGION_DIR} ${ATLAS_REGION_TESTS})
set (ATLAS_COPY_FILES ${ATLAS_COPY_FILES} ${ATLAS_REGION_TESTS})

set (ATLAS_PMALLOC_DIR "${ATLAS_TESTS_DIR}/pmalloc")
set (ATLAS_PMALLOC_TESTS test_pmalloc)
prepend_paths (ATLAS_PMALLOC_TESTS ${ATLAS_PMALLOC_DIR} ${ATLAS_PMALLOC_TESTS})
set (ATLAS_COPY_FILES ${ATLAS_COPY_FILES} ${ATLAS_PMALLOC_TESTS})

#default to debug build if no build type specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message ("No build type specified, defaulting to debug")
    set (CMAKE_BUILD_TYPE "Debug" CACHE STRING "Defaulted to debug build, none specified" FORCE)
endif ()

#use clang for everything
set (CMAKE_C_COMPILER "clang")
set (CMAKE_CXX_COMPILER "clang++")

#set flags for using instrumentation

set (INSTR $ENV{PLUGIN})
if (INSTR)
    message ("Found environment variable PLUGIN set to ${INSTR}")
elseif (EXISTS "${CMAKE_SOURCE_DIR}/../compiler-plugin/plugin_build/NvmInstrumenter.so")
    set (INSTR "${CMAKE_SOURCE_DIR}/../compiler-plugin/plugin_build/NvmInstrumenter.so")
    message ("Found a compiler plugin build in ${INSTR}")
else ()
    message (FATAL_ERROR "Cannot find NvmInstrumenter.so, is it built in Atlas/compiler-plugin/plugin_build? If built outside of Atlas/compiler-plugin/plugin_build set environment variable PLUGIN to it's location.")
endif ()
set (NVM_INSTR_FLAGS "-Xclang -load -Xclang ${INSTR}")

#debug settings
set (CMAKE_C_FLAGS_DEBUG "-g3 -DDEBUG")
set (CMAKE_CXX_FLAGS_DEBUG "-g3 -DDEBUG")

#Defines required for building Atlas
set (ATLAS_SPECIFIC_FLAGS "-DATLAS_ALLOC_DUMP -DATLAS_ALLOC_TRACE")

#Cmake defines used to specify different targets for Atlas - must pass to cmake as -DVAR_NAME=true
if (ALL_PERSISTENT)
    message ("Configuring for all persistent")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_ALL_PERSISTENT")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_ALL_PERSISTENT")
endif ()
if (NVM_STATS)
    message ("Configuring for nvm stats")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNVM_STATS")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DNVM_STATS")
endif ()
if (FLC)
    message ("Configuring for critical section level cache flush")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FLUSH_LOCAL_COMMIT")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FLUSH_LOCAL_COMMIT")
endif ()
if (FGC)
    message ("Configuring for global consistent point cache flush")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FLUSH_GLOBAL_COMMIT")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FLUSH_GLOBAL_COMMIT")
endif ()
if (USE_TABLE_FLUSH)
    message ("Configuring for table based flush")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_USE_TABLE_FLUSH")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_USE_TABLE_FLUSH")
endif ()
if (DISABLE_FLUSH)
    message ("Configuring disabling all cache line flushes")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDISABLE_FLUSHES")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DDISABLE_FLUSHES")
endif ()
if (DISABLE_HELPER)
    message ("Configuring disabling helper")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_HELPER")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_DISABLE_HELPER")
endif ()
if (DISABLE_ALLOC_LOGGING)
    message ("Configuring disabling alloc logging")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_ALLOC_LOGGING")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_DISABLE_ALLOC_LOGGING")
endif ()
if (DISABLE_LOG_FLUSH)
    message ("Configuring disabling log flush")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_LOG_FLUSH")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_DISABLE_LOG_FLUSH")
endif ()
if (DISABLE_DATA_FLUSH)
    message ("Configuring disabling data flush")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_DATA_FLUSH")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_DISABLE_DATA_FLUSH")
endif ()
if (PROFILE_HT)
    message ("Configuring profile helper thread")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_PROFILE_HT")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_PROFILE_HT")
endif ()
if (USE_MOVNT)
    message ("Configuring use movnt")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_USE_MOVNT")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_USE_MOVNT")
endif ()
if (NO_SFENCE)
    message ("Configuring for movnt use with no sfence")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_NO_SFENCE")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_NO_SFENCE")
endif ()
if (LOG_FLUSH_OPT)
    message ("Configuring to log flush optimization")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_LOG_FLUSH_OPT")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_LOG_FLUSH_OPT")
endif ()
if (LOG_WITH_NVM_ALLOC)
    message ("Configuring to log with nvm alloc")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_LOG_WITH_NVM_ALLOC")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_LOG_WITH_NVM_ALLOC")
endif ()
if (LOG_WITH_MALLOC)
    message ("Configuring to log with malloc")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_LOG_WITH_MALLOC")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_LOG_WITH_MALLOC")
endif ()
if (USE_MALLOC)
    message ("Configuring to use malloc")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_USE_MALLOC")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_USE_MALLOC")
endif ()
if (OPT_UNIQ_LOC)
    message ("Configuring to opt uniq loc")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_OPT_UNIQ_LOC")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_OPT_UNIQ_LOC")
endif ()
if (ALWAYS_LOG)
    message ("Configuring to always log")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_ALWAYS_LOG")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_ALWAYS_LOG")
endif ()
if (ALWAYS_MAP)
    message ("Configuring to always map")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_ALWAYS_MAP")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_ALWAYS_MAP")
endif ()
if (TRACE)
    message ("Configuring to use trace")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_NVM_TRACE")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_NVM_TRACE")
endif ()
if (FORCE_FAIL)
    message ("Configuring to perform process crash testing")
    set (APP_FLAGS "-D_FORCE_FAIL")
endif ()
if (FAIL_ATLAS)
    message ("Configuring atlas to fail internally")
    set (ATLAS_SPECIFIC_FLAGS "${ATLAS_SPECIFIC_FLAGS} -D_FORCE_FAIL -rdynamic")
endif ()
if (VERBOSE_TRACE)
    message ("Configuring for verbose trace")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_NVM_VERBOSE_TRACE")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_NVM_VERBOSE_TRACE")
endif ()
if (SRRF)
    message ("Configuring for strong restart race freedom")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_SRRF")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_SRRF")
endif ()
if (NVDIMM_PROLIANT)
    message ("Configuring for ProLiant NVDIMM")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_NVDIMM_PROLIANT")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_NVDIMM_PROLIANT")
endif ()    
if (NO_NEST)
    message ("Configuring for no nesting of critical sections")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_NO_NEST")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_NO_NEST")
endif ()
if (CONSISTENCY_FAIL)
    message ("Setting consistency tests to fail")
    set (CONSISTENCY_FAIL_FLAG "-DCONSISTENCY_FAIL")
endif ()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra -pedantic -Wno-unused-parameter -Winline ${ATLAS_SPECIFIC_FLAGS}")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -Wall -Wextra -pedantic -Winline ${ATLAS_SPECIFIC_FLAGS}")

#copy necessary files from the src dir to the build dir
foreach (t ${ATLAS_COPY_FILES})
    configure_file (${CMAKE_SOURCE_DIR}/${t} ${PROJECT_BINARY_DIR}/${t} COPYONLY)
endforeach (t)

include_directories (${CMAKE_SOURCE_DIR}/${ATLAS_INTERNAL_INCLUDES} ${CMAKE_SOURCE_DIR}/${ATLAS_INCLUDE_DIR})

set (LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

#variable to build only data structures in tests
if (NOT DATA_STRUCTS_ONLY)
    message ("Defaulting to build all tests")
    add_subdirectory (tests)
else ()
    message ("Building only data structures")
    add_subdirectory (tests/data_structures)
endif ()

add_subdirectory (src)
add_library (atlas $<TARGET_OBJECTS:Cache_flush> $<TARGET_OBJECTS:Consistency> $<TARGET_OBJECTS:Logger> $<TARGET_OBJECTS:Util> $<TARGET_OBJECTS:Pregion_mgr> $<TARGET_OBJECTS:Pmalloc>) #defaults to static build

#install ()
