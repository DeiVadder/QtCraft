# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)

# Oldest macOS the app runs on, also LSMinimumSystemVersion: the prebuilt Qt 6.12 libraries are
# built for 14.4 (E-945); the C++ library's std::format/std::print need 13.3.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 14.4)
endif()

project(QtCraft VERSION 1.1.0 DESCRIPTION "Voxel sandbox game built with Qt" LANGUAGES CXX)
if(APPLE)
    # The platform part of the mouse capture talks to AppKit.
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Concurrent Network Gui Multimedia Quick QuickControls2 ShaderTools LinguistTools Test)
# Since Qt 6.10 the private module targets (Qt::GuiPrivate, Qt::QuickPrivate) are packages of
# their own; before that, the public packages define them. Their use is deliberate and checked at
# every Qt update (E-945), so the private-module warning stays off.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.10)
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate QuickPrivate)
endif()

qt_standard_project_setup(REQUIRES 6.8 I18N_SOURCE_LANGUAGE en I18N_TRANSLATED_LANGUAGES de)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(QcProjectOptions)
include(QcGamepad)
include(QcTls)
include(QcWindowsResources)
include(QcDocs)

# The CI builds the tests only when it also runs them (they take hours on the hosted runners).
option(QC_BUILD_TESTS "Build the tests (tests/)" ON)
if(QC_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(3rdparty/stb_vorbis)
add_subdirectory(content)
add_subdirectory(src/common)
add_subdirectory(src/network)
add_subdirectory(src/worldgen)
add_subdirectory(src/server)
add_subdirectory(src/worlds)
add_subdirectory(src/clientcore)
add_subdirectory(src/headless)
add_subdirectory(src/audio)
add_subdirectory(src/app)
if(QC_BUILD_TESTS)
    add_subdirectory(tests)
endif()
