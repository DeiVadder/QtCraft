# Gamepad support through SDL3 (decision E-540). SDL3 comes from the submodule 3rdparty/SDL at the
# tag of 3rdparty/sdl3-tag.txt, built once by scripts/build-sdl.sh into a cache shared by every
# preset and worktree (~/.cache/qtcraft/sdl3/<tag>-<arch>-<config>, "universal" for arm64;x86_64).
# This file only finds it: the configure step builds it only when the cache entry is missing,
# normal builds and verify.sh never do. SDL3 is searched only in that entry, so a new tag, another
# architecture or new build options of the script always take their own entry; a cached SDL3_DIR
# of an earlier entry is dropped. Only an SDL3_DIR the user set outside the cache is taken as
# given. Either way the version must equal the tag. Sets QC_GAMEPAD_ENABLED and, when on, the
# imported target SDL3::SDL3-static; QC_SDL_ARCH is "universal", the one architecture of
# CMAKE_OSX_ARCHITECTURES, or "native".

option(QC_GAMEPAD "Gamepad support through SDL3 (off: the game builds without SDL and without gamepads)" ON)
option(QC_SDL_AUTO_BUILD "Run scripts/build-sdl.sh during configure when the SDL3 cache entry is missing" ON)

set(QC_GAMEPAD_ENABLED OFF)
set(QC_SDL_ARCH native)
if(NOT QC_GAMEPAD)
    message(STATUS "Gamepad support: off (QC_GAMEPAD=OFF)")
    return()
endif()

set(_qc_sdl_script "${PROJECT_SOURCE_DIR}/scripts/build-sdl.sh")
set(_qc_sdl_tag_file "${PROJECT_SOURCE_DIR}/3rdparty/sdl3-tag.txt")
# A new tag or new build options configure again and so pick their cache entry.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_qc_sdl_tag_file}" "${_qc_sdl_script}")
file(STRINGS "${_qc_sdl_tag_file}" _qc_sdl_tag LIMIT_COUNT 1)
string(STRIP "${_qc_sdl_tag}" _qc_sdl_tag)
string(REGEX REPLACE "^release-" "" _qc_sdl_wanted_version "${_qc_sdl_tag}")

set(_qc_sdl_args "")
list(LENGTH CMAKE_OSX_ARCHITECTURES _qc_arch_count)
if(APPLE AND _qc_arch_count GREATER 1)
    set(_qc_sdl_args --universal)
    set(QC_SDL_ARCH universal)
elseif(APPLE AND _qc_arch_count EQUAL 1)
    set(_qc_sdl_args --arch "${CMAKE_OSX_ARCHITECTURES}")
    set(QC_SDL_ARCH "${CMAKE_OSX_ARCHITECTURES}")
endif()

# MSVC: a Debug build links the debug C runtime (/MDd, CMake's default runtime for Debug), and a
# static SDL must link the same one, or the program gets two C runtimes (LNK4098); its own entry.
if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND _qc_sdl_args --debug-runtime)
endif()

# The script needs a POSIX shell: sh, on Windows the bash of Git for Windows (Git Bash; the MSVC
# environment of the configure step passes through, so SDL is built with the same compiler).
# Only Git's own folder: a bash.exe on PATH may be the one of WSL, which does not see the MSVC tools.
if(WIN32)
    find_program(QC_SDL_SHELL bash HINTS "$ENV{ProgramFiles}/Git/bin" NO_DEFAULT_PATH)
else()
    find_program(QC_SDL_SHELL sh)
endif()

# The cache directory comes from the script, so CMake and the script cannot disagree about it.
set(_qc_sdl_prefix "")
if(QC_SDL_SHELL)
    execute_process(COMMAND "${QC_SDL_SHELL}" "${_qc_sdl_script}" ${_qc_sdl_args} --print-prefix
        OUTPUT_VARIABLE _qc_sdl_prefix OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

# An SDL3_DIR inside the cache root of the script, or the one this file found last time
# (_QC_SDL3_DIR_FOUND), is ours and is dropped when it lies outside the current entry; any other
# set SDL3_DIR comes from the user (command line or cache editor) and is taken as given.
set(_qc_sdl_user_dir OFF)
if(DEFINED CACHE{SDL3_DIR})
    set(_qc_sdl_dir_in_prefix OFF)
    set(_qc_sdl_dir_in_root OFF)
    if(_qc_sdl_prefix)
        cmake_path(GET _qc_sdl_prefix PARENT_PATH _qc_sdl_cache_root)
        cmake_path(IS_PREFIX _qc_sdl_prefix "${SDL3_DIR}" NORMALIZE _qc_sdl_dir_in_prefix)
        cmake_path(IS_PREFIX _qc_sdl_cache_root "${SDL3_DIR}" NORMALIZE _qc_sdl_dir_in_root)
    endif()
    if(SDL3_DIR AND NOT _qc_sdl_dir_in_root AND NOT SDL3_DIR STREQUAL "${_QC_SDL3_DIR_FOUND}")
        set(_qc_sdl_user_dir ON)
    elseif(NOT _qc_sdl_dir_in_prefix)
        unset(SDL3_DIR CACHE)
        unset(SDL3_DIR)
    endif()
endif()

# The static library only: a shared SDL3 of the system would have to be bundled.
macro(_qc_find_sdl3)
    if(_qc_sdl_user_dir)
        find_package(SDL3 CONFIG QUIET COMPONENTS SDL3-static)
    elseif(_qc_sdl_prefix)
        find_package(SDL3 CONFIG QUIET COMPONENTS SDL3-static PATHS "${_qc_sdl_prefix}" NO_DEFAULT_PATH)
    endif()
endmacro()
_qc_find_sdl3()

if(NOT SDL3_FOUND AND NOT _qc_sdl_user_dir AND QC_SDL_AUTO_BUILD AND QC_SDL_SHELL)
    message(STATUS "SDL3 missing in ${_qc_sdl_prefix}: building it once (scripts/build-sdl.sh ${_qc_sdl_args})")
    find_program(_qc_nice nice)
    set(_qc_nice_command "")
    if(_qc_nice)
        set(_qc_nice_command "${_qc_nice}" -n 10)
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env "QC_SDL_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}" QC_SDL_WAIT_BENCH_LOCK=1
            ${_qc_nice_command} "${QC_SDL_SHELL}" "${_qc_sdl_script}" ${_qc_sdl_args}
        RESULT_VARIABLE _qc_sdl_result OUTPUT_QUIET)
    if(_qc_sdl_result EQUAL 0)
        unset(SDL3_DIR CACHE)
        unset(SDL3_DIR)
        _qc_find_sdl3()
    endif()
endif()

if(NOT SDL3_FOUND)
    if(WIN32 AND NOT QC_SDL_SHELL)
        message(FATAL_ERROR
            "SDL3 is built by scripts/build-sdl.sh, which needs the bash of Git for Windows (not found).\n"
            "Install Git for Windows, set QC_SDL_SHELL to its bin/bash.exe, set SDL3_DIR to a static SDL3 build,\n"
            "or configure without gamepad support:  -DQC_GAMEPAD=OFF")
    endif()
    message(FATAL_ERROR
        "SDL3 (static, ${_qc_sdl_prefix}) not found and could not be built.\n"
        "Build it once with:  scripts/build-sdl.sh ${_qc_sdl_args}\n"
        "or configure without gamepad support:  -DQC_GAMEPAD=OFF")
endif()

if(NOT SDL3_VERSION VERSION_EQUAL _qc_sdl_wanted_version)
    message(FATAL_ERROR
        "SDL3 in ${SDL3_DIR} is version ${SDL3_VERSION}, 3rdparty/sdl3-tag.txt wants ${_qc_sdl_tag}.\n"
        "Unset SDL3_DIR (-USDL3_DIR) to use the shared cache, or point it at an SDL3 ${_qc_sdl_wanted_version} build.")
endif()
if(NOT _qc_sdl_user_dir)
    set(_QC_SDL3_DIR_FOUND "${SDL3_DIR}" CACHE INTERNAL "SDL3_DIR as found in the SDL3 cache entry")
endif()

set(QC_GAMEPAD_ENABLED ON)
message(STATUS "Gamepad support: SDL3 ${SDL3_VERSION} from ${SDL3_DIR}")
