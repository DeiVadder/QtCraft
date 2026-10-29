# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Source groups that mirror the folder structure, so that IDEs reading the CMake file API
# (Qt Creator, VS Code CMake Tools, Xcode, Visual Studio) show a target's sources in their
# folders instead of the flat default groups "Source Files" and "Header Files" (decision E-640).
#
# qc_configure_target() registers every own target here. At the end of the directory that
# defines the target, after Qt's deferred finalizers have added their files, each target gets:
# - its sources below its own source directory as a tree relative to that directory
#   (qc_server: blocks/, chunks/, mobs/ …; a test: world/, gameplay/ … below tests/),
# - its other sources in the project as a tree relative to the project root,
# - sources added to it later from another directory (target_sources() of src/common for
#   qc_base_content) as a tree relative to the project root, through a fallback group for all
#   files below src/,
# - every file in the build directory (moc, qrc, qmlcache, type registration, shaders,
#   CMake rules) and the metatypes files Qt's libraries pass on in the group "Generated".
# Source groups belong to a directory, so the groups are set in the target's own directory.

# moc and rcc output joins the other generated files (qt_standard_project_setup() sets
# "Source Files/Generated").
set_property(GLOBAL PROPERTY AUTOGEN_SOURCE_GROUP "Generated")

# Registers a target for the folder groups of its directory.
function(qc_source_tree target)
    set_property(DIRECTORY APPEND PROPERTY QC_SOURCE_TREE_TARGETS ${target})

    get_property(scheduled DIRECTORY PROPERTY QC_SOURCE_TREE_SCHEDULED)
    if(NOT scheduled)
        set_property(DIRECTORY PROPERTY QC_SOURCE_TREE_SCHEDULED TRUE)
        cmake_language(DEFER CALL _qc_source_tree_apply)
    endif()
endfunction()

# Sets the groups of all targets registered in the current directory. Qt's finalizers add
# shaders and resources and may defer themselves again; the groups wait until none is queued.
function(_qc_source_tree_apply)
    cmake_language(DEFER GET_CALL_IDS queued_ids)
    foreach(id IN LISTS queued_ids)
        cmake_language(DEFER GET_CALL ${id} call)
        list(GET call 0 command)
        if(command MATCHES "^(qt6?_finalize_target)$")
            cmake_language(DEFER CALL _qc_source_tree_apply)
            return()
        endif()
    endforeach()

    string(REGEX REPLACE "([][+.*()^$?|\\\\])" "\\\\\\1" binary_dir_regex "${CMAKE_BINARY_DIR}")
    source_group("Generated" REGULAR_EXPRESSION "(^${binary_dir_regex}/|_metatypes\\.json$)")
    _qc_source_tree_fallback()

    get_property(targets DIRECTORY PROPERTY QC_SOURCE_TREE_TARGETS)
    foreach(target IN LISTS targets)
        _qc_source_tree_target(${target})
    endforeach()
endfunction()

# Groups every file below src/ by its path from the project root. A target's own groups,
# set afterwards, take precedence; the fallback covers sources added after the directory ended.
function(_qc_source_tree_fallback)
    get_property(files GLOBAL PROPERTY QC_SOURCE_TREE_FALLBACK_FILES)
    if(NOT files)
        file(GLOB_RECURSE files "${PROJECT_SOURCE_DIR}/src/*")
        set_property(GLOBAL PROPERTY QC_SOURCE_TREE_FALLBACK_FILES "${files}")
    endif()

    source_group(TREE "${PROJECT_SOURCE_DIR}" FILES ${files})
endfunction()

# Groups the sources of one target by folder.
function(_qc_source_tree_target target)
    get_target_property(target_dir ${target} SOURCE_DIR)
    get_target_property(sources ${target} SOURCES)
    if(NOT sources)
        return()
    endif()

    set(own_files)
    set(project_files)
    foreach(source IN LISTS sources)
        if(source MATCHES "\\$<")
            continue()
        endif()
        cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${target_dir}" NORMALIZE OUTPUT_VARIABLE file)

        cmake_path(IS_PREFIX CMAKE_BINARY_DIR "${file}" NORMALIZE in_build)
        cmake_path(IS_PREFIX target_dir "${file}" NORMALIZE in_target)
        cmake_path(IS_PREFIX PROJECT_SOURCE_DIR "${file}" NORMALIZE in_project)
        if(in_build)
            continue()
        elseif(in_target)
            list(APPEND own_files "${file}")
        elseif(in_project)
            list(APPEND project_files "${file}")
        endif()
    endforeach()


    if(own_files)
        source_group(TREE "${target_dir}" FILES ${own_files})
    endif()
    if(project_files)
        source_group(TREE "${PROJECT_SOURCE_DIR}" FILES ${project_files})
    endif()
endfunction()
