# Central compiler policy for every QtCraft-owned target (libraries, app, tests).
# Qt headers come in through imported targets and are therefore SYSTEM includes.

set(QC_SANITIZERS "" CACHE STRING "Sanitizers for all QtCraft targets: empty, 'address;undefined' or 'thread'")
option(QC_QML_DEBUG "Let the game accept a QML profiler/debugger (-qmljsdebugger); profiling builds only" OFF)

add_library(qc_project_options INTERFACE)

if(MSVC)
    target_compile_options(qc_project_options INTERFACE /W4 /WX /EHs-c- /permissive-)
    target_compile_definitions(qc_project_options INTERFACE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(qc_project_options INTERFACE
        -Wall -Wextra -Wpedantic -Werror
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

# Applies the project policy to an own target.
function(qc_configure_target target)
    target_link_libraries(${target} PRIVATE qc_project_options)
endfunction()

# Adds a QtTest executable registered with CTest.
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
    set_tests_properties(${name} PROPERTIES
        TIMEOUT ${ARG_TIMEOUT}
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data")
endfunction()
