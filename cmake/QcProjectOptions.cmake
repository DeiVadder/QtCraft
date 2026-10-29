# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Central compiler policy for every QtCraft-owned target (libraries, app, tests).
# Qt headers come in through imported targets and are therefore SYSTEM includes.

set(QC_SANITIZERS "" CACHE STRING "Sanitizers for all QtCraft targets: empty, 'address;undefined' or 'thread'")
option(QC_QML_DEBUG "Let the game accept a QML profiler/debugger (-qmljsdebugger); profiling builds only" OFF)
# Project libraries as shared libraries (decision E-890): the presets debug, asan and tsan switch it
# on, so the ~380 test programs share one (instrumented) copy of each library instead of linking
# their own. Release, deploy, profile and every Windows build stay static (no export macros, the
# deployed app unchanged).
option(QC_SHARED_LIBS "Build the project libraries (qc_common, qc_server, ...) as shared libraries" OFF)
if(QC_SHARED_LIBS AND WIN32)
    message(STATUS "QC_SHARED_LIBS: Windows builds keep static project libraries")
    set(QC_SHARED_LIBS OFF)
endif()
# The type every project library passes to qt_add_library.
if(QC_SHARED_LIBS)
    set(QC_LIBRARY_TYPE SHARED)
    # A changed library does not relink the programs that load it (the loader binds at start);
    # verify.sh --changed still selects their tests through the library's content (E-890).
    set(CMAKE_LINK_DEPENDS_NO_SHARED ON)
else()
    set(QC_LIBRARY_TYPE STATIC)
endif()

add_library(qc_project_options INTERFACE)
# Compile option for sources that must round alike on every compiler and CPU (no fused multiply-add
# contraction; decisions E-300, E-901): Clang and GCC need -ffp-contract=off, clang-cl the same
# through /clang:. MSVC (cl, Visual Studio 2022) contracts only with /fp:contract, so nothing.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(QC_NO_FP_CONTRACT "")
elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(QC_NO_FP_CONTRACT "/clang:-ffp-contract=off")
else()
    set(QC_NO_FP_CONTRACT "-ffp-contract=off")
endif()
# C++23 for every own target (a compile feature, so CMake picks the flag for the compiler).
target_compile_features(qc_project_options INTERFACE cxx_std_23)

if(MSVC)
    # The default /EHsc of CMake would only be overridden by /EHs-c- with warning D9025 per file.
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    # C4702 (unreachable code) comes from the optimizer inside Qt's inline code once exceptions are
    # off; /external:W0 does not reach it, and Clang/GCC have no such warning in -Wall -Wextra.
    target_compile_options(qc_project_options INTERFACE /W4 /WX /EHs-c- /permissive- /wd4702)
    target_compile_definitions(qc_project_options INTERFACE _HAS_EXCEPTIONS=0)
else()
    # -Wold-style-cast: casts only as named casts (ES.49, E-865); MSVC has no such warning.
    target_compile_options(qc_project_options INTERFACE
        -Wall -Wextra -Wpedantic -Wold-style-cast -Werror
        -fno-exceptions)
endif()

# Name hiding as MSVC reports it (C4456-C4459, decision E-898): Clang checks it only with -Wshadow,
# which -Wall -Wextra leave out. QC_WSHADOW=ON turns the matching Clang warnings on (as errors like
# every other warning), so a Mac build finds what the next Windows build would refuse.
# Not reported by MSVC and therefore left out: a field hiding one of a base class (-Wshadow-field) and
# a lambda parameter hiding a local the lambda does not capture (-Wshadow-uncaptured-local).
option(QC_WSHADOW "Clang: report names that hide a local, parameter, member or global (MSVC C4456-C4459)" OFF)
if(QC_WSHADOW AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(qc_project_options INTERFACE -Wshadow-all -Wno-shadow-field -Wno-shadow-uncaptured-local)
elseif(QC_WSHADOW AND NOT MSVC)
    message(WARNING "QC_WSHADOW needs Clang (-Wshadow-all); ignored for ${CMAKE_CXX_COMPILER_ID}")
endif()

target_compile_definitions(qc_project_options INTERFACE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_URL_CAST_FROM_STRING
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
    QT_USE_QSTRINGBUILDER)

if(QC_SANITIZERS AND MSVC)
    message(FATAL_ERROR "QC_SANITIZERS supports Clang/GCC only")
elseif(QC_SANITIZERS)
    list(JOIN QC_SANITIZERS "," _qc_sanitizer_list)
    target_compile_options(qc_project_options INTERFACE
        -fsanitize=${_qc_sanitizer_list} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(qc_project_options INTERFACE -fsanitize=${_qc_sanitizer_list})
    # Line tables are all a sanitizer report needs for file, line and inlined frames (E-890); the
    # full debug information of -g made the object files of the sanitizer trees several times larger.
    # The last -g option wins, so this follows the -g of CMAKE_CXX_FLAGS_DEBUG.
    target_compile_options(qc_project_options INTERFACE
        $<IF:$<CXX_COMPILER_ID:Clang,AppleClang>,-gline-tables-only,-g1>)
endif()

# Linux linkers may drop a shared library nothing references directly (--as-needed); the resource
# libraries (qc_texture_packs) register their files only through a static initializer.
if(QC_SHARED_LIBS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(qc_project_options INTERFACE LINKER:--no-as-needed)
endif()

# ccache for every preset when it is installed (E-890; `brew install ccache`); QC_CCACHE=OFF or a
# launcher of one's own (CMAKE_CXX_COMPILER_LAUNCHER) turns it off.
option(QC_CCACHE "Compile through ccache when it is installed" ON)
if(QC_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(QC_CCACHE_PROGRAM ccache)
    if(QC_CCACHE_PROGRAM)
        message(STATUS "Compiling through ${QC_CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${QC_CCACHE_PROGRAM}")
        set(CMAKE_OBJCXX_COMPILER_LAUNCHER "${QC_CCACHE_PROGRAM}")
    endif()
endif()

include(QcSourceTree)

# Applies the project policy to an own target and groups its sources by folder for IDEs.
function(qc_configure_target target)
    target_link_libraries(${target} PRIVATE qc_project_options)
    qc_source_tree(${target})
endfunction()

# Adds a QtTest executable registered with CTest. Every test gets its own data directory
# (build/<preset>/test-data/<name>) so that tests can run in parallel (`ctest -j`, decision E-380).
# The time limit holds for the whole test and, through QTEST_FUNCTION_TIMEOUT, for each test
# function: QtTest's own five minutes per function would otherwise end a slow function of a test
# with a longer limit in a parallel sanitizer run (decision E-410).
function(qc_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "TIMEOUT" "SOURCES;LIBRARIES")
    qt_add_executable(${name} ${ARG_SOURCES})
    qc_configure_target(${name})
    set_target_properties(${name} PROPERTIES MACOSX_BUNDLE OFF)
    target_link_libraries(${name} PRIVATE Qt::Test ${ARG_LIBRARIES})
    add_test(NAME ${name} COMMAND ${name})
    if(NOT ARG_TIMEOUT)
        set(ARG_TIMEOUT 120)
    endif()
    math(EXPR function_timeout_ms "${ARG_TIMEOUT} * 1000")
    set_tests_properties(${name} PROPERTIES
        TIMEOUT ${ARG_TIMEOUT}
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/${name};QTEST_FUNCTION_TIMEOUT=${function_timeout_ms}")
endfunction()
