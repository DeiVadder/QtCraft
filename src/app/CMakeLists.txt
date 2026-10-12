qt_add_executable(qtcraft
    main.cpp
    hooks/QcScreenshotHook.h hooks/QcScreenshotHook.cpp
    hooks/QcOffscreenRender.h hooks/QcOffscreenRender.cpp
    controllers/QcGameController.h controllers/QcGameController.cpp
    controllers/QcMultiplayerController.h controllers/QcMultiplayerController.cpp
    multiplayer/QcMultiplayerSettings.h multiplayer/QcMultiplayerSettings.cpp
    multiplayer/QcServerHistory.h multiplayer/QcServerHistory.cpp
    multiplayer/QcPlayerList.h multiplayer/QcPlayerList.cpp
    multiplayer/QcLanAddresses.h multiplayer/QcLanAddresses.cpp
    controllers/QcSessionParts.h
    controllers/QcSettingsController.h controllers/QcSettingsController.cpp
    controllers/QcAudioSettings.h controllers/QcAudioSettings.cpp
    controllers/QcDebugSettings.h controllers/QcDebugSettings.cpp
    controllers/QcCharacterSettings.h controllers/QcCharacterSettings.cpp
    controllers/QcPreviewSource.h controllers/QcPreviewSource.cpp
    controllers/QcWorldLibraryController.h controllers/QcWorldLibraryController.cpp
    controllers/QcPlayerController.h controllers/QcPlayerController.cpp
    controllers/QcHudState.h controllers/QcHudState.cpp
    controllers/QcDebugHud.h controllers/QcDebugHud.cpp
    controllers/QcDebugVisuals.h controllers/QcDebugVisuals.cpp
    controllers/QcHandbookController.h controllers/QcHandbookController.cpp
    models/QcItemIcons.h models/QcItemIcons.cpp
    models/QcUiTheme.h
    models/QcControlPalette.h models/QcControlPalette.cpp
    input/QcGameInput.h input/QcGameInput.cpp
    rendering/QcRenderBridge.h rendering/QcRenderBridge.cpp
    hooks/QcStartupHooks.h hooks/QcStartupHooks.cpp
    hooks/QcBenchmarkRun.h hooks/QcBenchmarkRun.cpp
    models/QcInventoryModel.h models/QcInventoryModel.cpp
    input/QcKeyControlsForeign.h
    models/QcHandbookModel.h models/QcHandbookModel.cpp
    models/QcSlotRange.h models/QcSlotRange.cpp
    models/QcWorldLabelModel.h models/QcWorldLabelModel.cpp
    models/QcItemIconProvider.h models/QcItemIconProvider.cpp
    rendering/QcWorldRenderer.h rendering/QcWorldRenderer.cpp
    rendering/QcWorldView.h rendering/QcWorldView.cpp
    rendering/QcRenderPipelines.h rendering/QcRenderPipelines.cpp
    rendering/QcCharacterPreview.h rendering/QcCharacterPreview.cpp
    rendering/QcCharacterPreviewRenderer.h rendering/QcCharacterPreviewRenderer.cpp
    rendering/QcPreviewAtlasCache.h rendering/QcPreviewAtlasCache.cpp
    rendering/QcSkyRenderer.h rendering/QcSkyRenderer.cpp
    rendering/QcGraphItem.h rendering/QcGraphItem.cpp
    rendering/QcDebugLineRenderer.h rendering/QcDebugLineRenderer.cpp
    rendering/QcRenderSample.h
    hooks/QcFrameProfile.h hooks/QcFrameProfile.cpp
    hooks/QcFrameMonitor.h hooks/QcFrameMonitor.cpp
    hooks/QcThreadCpuClock.h hooks/QcThreadCpuClock.cpp
    input/QcRelativeMouse.h
    hooks/QcScriptedRun.h
)
if(APPLE)
    target_sources(qtcraft PRIVATE input/QcRelativeMouse_mac.mm hooks/QcScriptedRun_mac.mm)
    target_link_libraries(qtcraft PRIVATE "-framework AppKit" "-framework ApplicationServices")
else()
    target_sources(qtcraft PRIVATE input/QcRelativeMouse_other.cpp hooks/QcScriptedRun_other.cpp)
endif()
qc_configure_target(qtcraft)
# Classes live in thematic subfolders (E-089); every folder is an include directory, so includes
# stay "QcName.h" without a path.
target_include_directories(qtcraft PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/controllers
    ${CMAKE_CURRENT_SOURCE_DIR}/models
    ${CMAKE_CURRENT_SOURCE_DIR}/rendering
    ${CMAKE_CURRENT_SOURCE_DIR}/hooks
    ${CMAKE_CURRENT_SOURCE_DIR}/input
    ${CMAKE_CURRENT_SOURCE_DIR}/multiplayer)
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
    Qt::Concurrent Qt::Gui Qt::GuiPrivate Qt::Quick Qt::QuickControls2)

qt_add_qml_module(qtcraft
    URI QtCraft
    QML_FILES
        qml/Main.qml
        qml/CharacterScreen.qml
        qml/CharacterPresets.qml
        qml/CharacterPreviewPanel.qml
        qml/CharacterPreviewHost.qml
        qml/ProfileCard.qml
        qml/ChatOverlay.qml
        qml/ControlsPage.qml
        qml/DeathScreen.qml
        qml/FurnaceScreen.qml
        qml/HandbookScreen.qml
        qml/HandbookRecipe.qml
        qml/HandbookLink.qml
        qml/ChestScreen.qml
        qml/Hud.qml
        qml/InventoryScreen.qml
        qml/ItemSlot.qml
        qml/InventorySlot.qml
        qml/PlayerSlots.qml
        qml/SlotScreen.qml
        qml/HudLabel.qml
        qml/ThemedToolTip.qml
        qml/PauseMenu.qml
        qml/MainMenu.qml
        qml/MessageScreen.qml
        qml/ModsScreen.qml
        qml/SettingsScreen.qml
        qml/WorldLabels.qml
        qml/DebugOverlay.qml
        qml/DebugOptionsPage.qml
        qml/MultiplayerScreen.qml
        qml/RecentServerRow.qml
        qml/LanScreen.qml
        qml/PlayerListOverlay.qml
        qml/DisconnectScreen.qml
)

qt_add_shaders(qtcraft "qtcraft_shaders"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}
    PREFIX "/"
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/block.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/block.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/outline.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/outline.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/debugline.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/debugline.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/entity.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/entity.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hand.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag
)

qt_add_translations(qtcraft
    SOURCE_TARGETS qtcraft qc_clientcore qc_server qc_network
    TS_FILE_DIR ${PROJECT_SOURCE_DIR}/translations
    TS_FILE_BASE qtcraft
    RESOURCE_PREFIX /i18n)
