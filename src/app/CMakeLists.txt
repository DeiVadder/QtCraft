qt_add_executable(qtcraft
    main.cpp
    QcScreenshotHook.h QcScreenshotHook.cpp
    QcGameController.h QcGameController.cpp
    QcInventoryModel.h QcInventoryModel.cpp
    QcHandbookModel.h QcHandbookModel.cpp
    QcSlotRange.h QcSlotRange.cpp
    QcItemIconProvider.h QcItemIconProvider.cpp
    QcWorldRenderer.h QcWorldRenderer.cpp
    QcWorldView.h QcWorldView.cpp
    QcRenderSample.h
    QcFrameProfile.h QcFrameProfile.cpp
    QcThreadCpuClock.h QcThreadCpuClock.cpp
    QcRelativeMouse.h
    QcScriptedRun.h
)
if(APPLE)
    target_sources(qtcraft PRIVATE QcRelativeMouse_mac.mm QcScriptedRun_mac.mm)
    target_link_libraries(qtcraft PRIVATE "-framework AppKit" "-framework ApplicationServices")
else()
    target_sources(qtcraft PRIVATE QcRelativeMouse_other.cpp QcScriptedRun_other.cpp)
endif()
qc_configure_target(qtcraft)
target_compile_definitions(qtcraft PRIVATE QTCRAFT_VERSION="${PROJECT_VERSION}")
if(QC_QML_DEBUG)
    target_compile_definitions(qtcraft PRIVATE QT_QML_DEBUG)
endif()
# CFBundleVersion: the build number release builds pass (scripts/release.sh: UTC build time, e.g. 2026.1009.1130).
set(QC_BUILD_NUMBER "${PROJECT_VERSION}" CACHE STRING "Build number of the app bundle (CFBundleVersion)")
set_target_properties(qtcraft PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/cmake/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME QtCraft
    MACOSX_BUNDLE_GUI_IDENTIFIER org.qtcraft.QtCraft
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${QC_BUILD_NUMBER}
    MACOSX_BUNDLE_INFO_STRING "QtCraft ${PROJECT_VERSION}, a voxel sandbox game built with Qt"
    MACOSX_BUNDLE_COPYRIGHT "Copyright 2026 the QtCraft authors"
    MACOSX_BUNDLE_ICON_FILE QtCraft.icns
    WIN32_EXECUTABLE ON)

if(APPLE)
    # The icon is drawn from the base game's procedural textures (qtcraft-headless --export-icon)
    # and packed by iconutil into Contents/Resources/QtCraft.icns.
    set(QC_ICONSET ${CMAKE_CURRENT_BINARY_DIR}/QtCraft.iconset)
    set(QC_ICNS ${CMAKE_CURRENT_BINARY_DIR}/QtCraft.icns)
    add_custom_command(OUTPUT ${QC_ICNS}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${QC_ICONSET}
        COMMAND $<TARGET_FILE:qtcraft-headless> --export-icon ${QC_ICONSET}
        COMMAND iconutil --convert icns --output ${QC_ICNS} ${QC_ICONSET}
        DEPENDS qtcraft-headless
        COMMENT "Drawing the application icon"
        VERBATIM)
    target_sources(qtcraft PRIVATE ${QC_ICNS})
    set_source_files_properties(${QC_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

target_link_libraries(qtcraft PRIVATE
    qc_common qc_server qc_clientcore qc_audio
    Qt::Gui Qt::GuiPrivate Qt::Quick Qt::QuickControls2)

qt_add_qml_module(qtcraft
    URI QtCraft
    QML_FILES
        qml/Main.qml
        qml/CharacterScreen.qml
        qml/ChatOverlay.qml
        qml/DeathScreen.qml
        qml/FurnaceScreen.qml
        qml/HandbookScreen.qml
        qml/HandbookRecipe.qml
        qml/HandbookLink.qml
        qml/ChestScreen.qml
        qml/Hud.qml
        qml/InventoryScreen.qml
        qml/ItemSlot.qml
        qml/PauseMenu.qml
        qml/MainMenu.qml
        qml/MessageScreen.qml
        qml/ModsScreen.qml
        qml/SettingsScreen.qml
)

qt_add_shaders(qtcraft "qtcraft_shaders"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}
    PREFIX "/"
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/block.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/block.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/outline.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/outline.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/entity.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/entity.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hand.vert
)

qt_add_translations(qtcraft
    TS_FILE_DIR ${PROJECT_SOURCE_DIR}/translations
    TS_FILE_BASE qtcraft
    RESOURCE_PREFIX /i18n)
