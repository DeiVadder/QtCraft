# Base game content, embedded as a Qt resource under :/content/qtcraft. It is loaded through the same
# content-pack loader as mods.
file(GLOB_RECURSE QC_BASE_CONTENT_FILES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/qtcraft/*.json
    ${CMAKE_CURRENT_SOURCE_DIR}/qtcraft/*.png)

qt_add_library(qc_base_content STATIC)
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

qt_add_library(qc_texture_packs STATIC)
qc_configure_target(qc_texture_packs)
qt_add_resources(qc_texture_packs "qc_texture_packs"
    PREFIX /
    FILES ${QC_TEXTURE_PACK_FILES})
