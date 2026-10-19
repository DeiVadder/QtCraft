# The game's C++ side as a static library (decision E-417): controllers, models, rendering, input,
# hooks and the multiplayer screens, used by the game and by the tests. It is the backing target of
# the QML module QtCraft as well, because qmltyperegistrar registers only the QML types of the
# module's own target; the game links the module's static plugin, which registers them at start.
qt_add_library(qc_app_core STATIC)
# Sources per folder, sorted, each header next to its sources (scripts/check-cmake-lists.py).
target_sources(qc_app_core PRIVATE
    controllers/QcAudioSettings.h controllers/QcAudioSettings.cpp
    controllers/QcCharacterSettings.h controllers/QcCharacterSettings.cpp
    controllers/QcGameController.h controllers/QcGameController.cpp
    controllers/QcHandbookController.h controllers/QcHandbookController.cpp
    controllers/QcHudState.h controllers/QcHudState.cpp
    controllers/QcMultiplayerController.h controllers/QcMultiplayerController.cpp
    controllers/QcPlayerController.h controllers/QcPlayerController.cpp
    controllers/QcScreenState.h controllers/QcScreenState.cpp
    controllers/QcSettingsController.h controllers/QcSettingsController.cpp
    controllers/QcTexturePackController.h controllers/QcTexturePackController.cpp
    controllers/QcTradeController.h controllers/QcTradeController.cpp
    controllers/QcWorldLibraryController.h controllers/QcWorldLibraryController.cpp
)
target_sources(qc_app_core PRIVATE
    debug/QcDebugHud.h debug/QcDebugHud.cpp
    debug/QcDebugSettings.h debug/QcDebugSettings.cpp
    debug/QcDebugVisuals.h debug/QcDebugVisuals.cpp
    debug/QcGraphItem.h debug/QcGraphItem.cpp
)
target_sources(qc_app_core PRIVATE
    hooks/QcBenchmarkRun.h hooks/QcBenchmarkRun.cpp
    hooks/QcFrameMonitor.h hooks/QcFrameMonitor.cpp
    hooks/QcFrameProfile.h hooks/QcFrameProfile.cpp
    hooks/QcLayoutCheck.h hooks/QcLayoutCheck.cpp
    hooks/QcOffscreenRender.h hooks/QcOffscreenRender.cpp
    hooks/QcPackSwitchHitch.h hooks/QcPackSwitchHitch.cpp
    hooks/QcScreenshotHook.h hooks/QcScreenshotHook.cpp
    hooks/QcScriptedPad.h hooks/QcScriptedPad.cpp
    hooks/QcStartupHooks.h hooks/QcStartupHooks.cpp
    hooks/QcThreadCpuClock.h hooks/QcThreadCpuClock.cpp
)
target_sources(qc_app_core PRIVATE
    input/QcFocusCandidates.h input/QcFocusCandidates.cpp
    input/QcGameInput.h input/QcGameInput.cpp
    input/QcGamepadBackend.h input/QcGamepadBackend.cpp
    input/QcGamepadUi.h input/QcGamepadUi.cpp
    input/QcKeyControlsForeign.h
    input/QcMenuNavigator.h input/QcMenuNavigator.cpp
    input/QcRelativeMouse.h
)
target_sources(qc_app_core PRIVATE
    models/QcControlPalette.h models/QcControlPalette.cpp
    models/QcCreativeItemModel.h models/QcCreativeItemModel.cpp
    models/QcHandbookModel.h models/QcHandbookModel.cpp
    models/QcInventoryModel.h models/QcInventoryModel.cpp
    models/QcItemIconProvider.h models/QcItemIconProvider.cpp
    models/QcItemIcons.h models/QcItemIcons.cpp
    models/QcSlotRange.h models/QcSlotRange.cpp
    models/QcTexturePackPreviewProvider.h models/QcTexturePackPreviewProvider.cpp
    models/QcUiTheme.h
    models/QcWorldLabelModel.h models/QcWorldLabelModel.cpp
)
target_sources(qc_app_core PRIVATE
    multiplayer/QcLanAddresses.h multiplayer/QcLanAddresses.cpp
    multiplayer/QcMultiplayerSettings.h multiplayer/QcMultiplayerSettings.cpp
    multiplayer/QcNetworkClientForeign.h
    multiplayer/QcPlayerList.h multiplayer/QcPlayerList.cpp
    multiplayer/QcServerHistory.h multiplayer/QcServerHistory.cpp
)
target_sources(qc_app_core PRIVATE
    preview/QcCharacterPreview.h preview/QcCharacterPreview.cpp
    preview/QcCharacterPreviewRenderer.h preview/QcCharacterPreviewRenderer.cpp
    preview/QcPreviewAtlasCache.h preview/QcPreviewAtlasCache.cpp
    preview/QcPreviewSource.h preview/QcPreviewSource.cpp
)
target_sources(qc_app_core PRIVATE
    rendering/QcAtlasTexture.h rendering/QcAtlasTexture.cpp
    rendering/QcDebugLineRenderer.h rendering/QcDebugLineRenderer.cpp
    rendering/QcEntityFrameBuilder.h rendering/QcEntityFrameBuilder.cpp
    rendering/QcFramePlan.h
    rendering/QcGpuMeshStore.h rendering/QcGpuMeshStore.cpp
    rendering/QcPrecipitationRenderer.h rendering/QcPrecipitationRenderer.cpp
    rendering/QcRenderBridge.h rendering/QcRenderBridge.cpp
    rendering/QcRenderPipelines.h rendering/QcRenderPipelines.cpp
    rendering/QcRenderSample.h
    rendering/QcSectionLightPool.h rendering/QcSectionLightPool.cpp
    rendering/QcSkyRenderer.h rendering/QcSkyRenderer.cpp
    rendering/QcWorldRenderer.h rendering/QcWorldRenderer.cpp
    rendering/QcWorldView.h rendering/QcWorldView.cpp
)
target_sources(qc_app_core PRIVATE
    session/QcFrameDriver.h session/QcFrameDriver.cpp
    session/QcSessionParts.h
    session/QcSessionWiring.h session/QcSessionWiring.cpp
    session/QcWorldAtlas.h session/QcWorldAtlas.cpp
    session/QcWorldStarter.h session/QcWorldStarter.cpp
)
# The mouse capture talks to the platform (AppKit on macOS).
if(APPLE)
    target_sources(qc_app_core PRIVATE input/QcRelativeMouse_mac.mm)
    target_link_libraries(qc_app_core PRIVATE "-framework AppKit" "-framework ApplicationServices")
else()
    target_sources(qc_app_core PRIVATE input/QcRelativeMouse_other.cpp)
endif()
# The SDL3 gamepad backend, statically linked, only with QC_GAMEPAD (cmake/QcGamepad.cmake, E-540).
if(QC_GAMEPAD_ENABLED)
    target_sources(qc_app_core PRIVATE input/QcSdlGamepadSource.h input/QcSdlGamepadSource.cpp)
    target_link_libraries(qc_app_core PRIVATE SDL3::SDL3-static)
endif()
target_compile_definitions(qc_app_core PRIVATE QC_HAVE_SDL_GAMEPAD=$<BOOL:${QC_GAMEPAD_ENABLED}>)
qc_configure_target(qc_app_core)
# Includes name the header by its path below src/ ("app/rendering/QcFramePlan.h", decision E-418).
# The include directory is a target of its own: a test that uses only headers of the app
# (test_frame_plan) links it instead of the whole library (decision E-417).
add_library(qc_app_core_headers INTERFACE)
target_include_directories(qc_app_core_headers INTERFACE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qc_app_core PUBLIC
    qc_app_core_headers qc_common qc_server qc_clientcore qc_audio
    Qt::Gui Qt::GuiPrivate Qt::Quick Qt::QuickControls2)

qt_add_qml_module(qc_app_core
    URI QtCraft
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/QtCraft
    QML_FILES
        qml/CharacterPresets.qml
        qml/CharacterPreviewHost.qml
        qml/CharacterPreviewPanel.qml
        qml/CharacterScreen.qml
        qml/ChatOverlay.qml
        qml/ChestScreen.qml
        qml/ControlsPage.qml
        qml/CreativeScreen.qml
        qml/DeathScreen.qml
        qml/DebugOptionsPage.qml
        qml/DebugOverlay.qml
        qml/DisconnectScreen.qml
        qml/FocusRing.qml
        qml/FurnaceScreen.qml
        qml/HandbookLink.qml
        qml/HandbookRecipe.qml
        qml/HandbookScreen.qml
        qml/HotbarRadial.qml
        qml/Hud.qml
        qml/HudLabel.qml
        qml/InventoryScreen.qml
        qml/InventorySlot.qml
        qml/ItemSlot.qml
        qml/LanScreen.qml
        qml/Main.qml
        qml/MainMenu.qml
        qml/MessageScreen.qml
        qml/ModsScreen.qml
        qml/MultiplayerScreen.qml
        qml/OnScreenKeyboard.qml
        qml/PadHints.qml
        qml/PauseMenu.qml
        qml/PlayerListOverlay.qml
        qml/PlayerSlots.qml
        qml/ProfileCard.qml
        qml/RecentServerRow.qml
        qml/SettingsScreen.qml
        qml/SlotScreen.qml
        qml/TexturePacksPage.qml
        qml/ThemedScrollBar.qml
        qml/ThemedToolTip.qml
        qml/TradeScreen.qml
        qml/WorldLabels.qml
)
# qmltyperegistrar includes the headers of the QML types by file name only (QTBUG-93443): only its
# generated file sees the folders of those headers, the code includes by path below src/ (E-418).
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/qc_app_core_qmltyperegistrations.cpp
    TARGET_DIRECTORY qc_app_core
    PROPERTIES INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_SOURCE_DIR}/controllers;${CMAKE_CURRENT_SOURCE_DIR}/debug;${CMAKE_CURRENT_SOURCE_DIR}/input;${CMAKE_CURRENT_SOURCE_DIR}/models;${CMAKE_CURRENT_SOURCE_DIR}/multiplayer;${CMAKE_CURRENT_SOURCE_DIR}/preview;${CMAKE_CURRENT_SOURCE_DIR}/rendering")

# The executable: start-up (command line, hooks of a scripted run), shaders, translations, icon.
qt_add_executable(qtcraft
    hooks/QcScriptedRun.h
    startup/main.cpp
    startup/QcAppLanguage.h startup/QcAppLanguage.cpp
    startup/QcAppOptions.h startup/QcAppOptions.cpp
    startup/QcAppStartup.h startup/QcAppStartup.cpp
    startup/QcAppWindow.h startup/QcAppWindow.cpp
)
if(APPLE)
    target_sources(qtcraft PRIVATE hooks/QcScriptedRun_mac.mm)
    target_link_libraries(qtcraft PRIVATE "-framework AppKit" "-framework ApplicationServices")
else()
    target_sources(qtcraft PRIVATE hooks/QcScriptedRun_other.cpp)
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

target_link_libraries(qtcraft PRIVATE qc_app_core qc_app_coreplugin)

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
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/precipitation.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/precipitation.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/entity.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/entity.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hand.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag
)
# The block fragment shader of the opaque pass writes alpha 1 (decision E-332).
qt_add_shaders(qtcraft "qtcraft_opaque_shaders"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}
    PREFIX "/"
    DEFINES QC_OPAQUE=1
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/block.frag
    OUTPUTS
        shaders/block_opaque.frag.qsb
)

qt_add_translations(qtcraft
    SOURCE_TARGETS qtcraft qc_app_core qc_clientcore qc_server qc_network
    TS_FILE_DIR ${PROJECT_SOURCE_DIR}/translations
    TS_FILE_BASE qtcraft
    RESOURCE_PREFIX /i18n)
