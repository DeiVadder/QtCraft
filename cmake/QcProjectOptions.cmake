# Central compiler policy for every QtCraft-owned target (libraries, app, tests).
# Qt headers come in through imported targets and are therefore SYSTEM includes.

set(QC_SANITIZERS "" CACHE STRING "Sanitizers for all QtCraft targets: empty, 'address;undefined' or 'thread'")
option(QC_QML_DEBUG "Let the game accept a QML profiler/debugger (-qmljsdebugger); profiling builds only" OFF)

add_library(qc_project_options INTERFACE)
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
