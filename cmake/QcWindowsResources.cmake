# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Windows resources of the executables (decision E-875): the procedural application icon
# cmake/windows/QtCraft.ico (written by `qtcraft-headless --export-icon cmake/windows/QtCraft.ico`;
# test_app_icon checks it against the textures) for Explorer, the taskbar and the title bar, and
# the version information Explorer shows (product, version, company, description).
if(WIN32)
    enable_language(RC)
endif()

function(qc_add_windows_resources target description)
    if(NOT WIN32)
        return()
    endif()
    set(QC_RC_NAME "${target}")
    set(QC_RC_DESCRIPTION "${description}")
    set(QC_RC_ICON "${PROJECT_SOURCE_DIR}/cmake/windows/QtCraft.ico")
    set(rc "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/windows/qtcraft.rc.in" "${rc}" @ONLY)
    target_sources(${target} PRIVATE "${rc}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${QC_RC_ICON}")
endfunction()
