cmake_minimum_required(VERSION 3.25)

project(QtCraft VERSION 0.1.0 DESCRIPTION "Voxel sandbox game built with Qt" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Concurrent Network Gui Quick QuickControls2 ShaderTools LinguistTools Test)

qt_standard_project_setup(REQUIRES 6.8 I18N_SOURCE_LANGUAGE en I18N_TRANSLATED_LANGUAGES de)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(QcProjectOptions)

enable_testing()

add_subdirectory(src/common)
add_subdirectory(src/server)
add_subdirectory(src/clientcore)
add_subdirectory(src/app)
add_subdirectory(tests)
