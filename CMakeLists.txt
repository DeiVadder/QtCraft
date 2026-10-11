cmake_minimum_required(VERSION 3.25)

# Oldest macOS the app runs on, also LSMinimumSystemVersion: Qt 6.8 needs 12, the floating-point
# formatting of std::format/std::print in the C++ library 13.3.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.3)
endif()

project(QtCraft VERSION 0.1.0 DESCRIPTION "Voxel sandbox game built with Qt" LANGUAGES CXX)
if(APPLE)
    # The platform part of the mouse capture talks to AppKit.
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Concurrent Network Gui Multimedia Quick QuickControls2 ShaderTools LinguistTools Test)

qt_standard_project_setup(REQUIRES 6.8 I18N_SOURCE_LANGUAGE en I18N_TRANSLATED_LANGUAGES de)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(QcProjectOptions)

enable_testing()

add_subdirectory(content)
add_subdirectory(src/common)
add_subdirectory(src/network)
add_subdirectory(src/server)
add_subdirectory(src/clientcore)
add_subdirectory(src/headless)
add_subdirectory(src/audio)
add_subdirectory(src/app)
add_subdirectory(tests)
