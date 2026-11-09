# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Base game content, embedded as a Qt resource under :/content/qtcraft. It is loaded through the same
# content-pack loader as mods.
file(GLOB_RECURSE QC_BASE_CONTENT_FILES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/qtcraft/*.json
    ${CMAKE_CURRENT_SOURCE_DIR}/qtcraft/*.png)
# The music is the client's alone (qc_music below).
list(FILTER QC_BASE_CONTENT_FILES EXCLUDE REGEX "^qtcraft/music/")

qt_add_library(qc_base_content ${QC_LIBRARY_TYPE})
qc_configure_target(qc_base_content)
qt_add_resources(qc_base_content "qc_base_content"
    PREFIX /content
    FILES ${QC_BASE_CONTENT_FILES})

# Built-in texture packs (decision E-297), embedded under :/texturepacks/<folder>; the client lists
# them next to the packs in the player's data folder.
file(GLOB_RECURSE QC_TEXTURE_PACK_FILES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/texturepacks/*.json
    ${CMAKE_CURRENT_SOURCE_DIR}/texturepacks/*.png)

qt_add_library(qc_texture_packs ${QC_LIBRARY_TYPE})
qc_configure_target(qc_texture_packs)
# The resource registration calls into Qt Core (a shared library resolves it at its own link).
target_link_libraries(qc_texture_packs PRIVATE Qt::Core)
qt_add_resources(qc_texture_packs "qc_texture_packs"
    PREFIX /
    FILES ${QC_TEXTURE_PACK_FILES})

# The music tracks of the base game with their descriptions (decision E-1136), under
# :/content/qtcraft/music beside the base content; only the game links them (some 70 MB), not the
# server, the headless program or the tests. Stored without compression (Ogg Vorbis is compressed
# already), so that QcVorbisStream decodes them where they lie; as object files (BIG_RESOURCES),
# since generated C++ of that size compiles slowly.
file(GLOB QC_MUSIC_FILES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/qtcraft/music/*.json
    ${CMAKE_CURRENT_SOURCE_DIR}/qtcraft/music/*.ogg)

qt_add_library(qc_music ${QC_LIBRARY_TYPE})
qc_configure_target(qc_music)
target_link_libraries(qc_music PRIVATE Qt::Core)
qt_add_resources(qc_music "qc_music"
    PREFIX /content
    BIG_RESOURCES
    OPTIONS --no-compress
    FILES ${QC_MUSIC_FILES})
