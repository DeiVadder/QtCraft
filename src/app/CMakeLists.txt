# The game's C++ side as a library (decision E-417; shared with QC_SHARED_LIBS, E-890): controllers,
# models, rendering, input, hooks and the multiplayer screens, used by the game and by the tests. It
# is the backing target of the QML module QtCraft as well, because qmltyperegistrar registers only
# the QML types of the module's own target; programs that load the module link qc_app_qml (below).
qt_add_library(qc_app_core ${QC_LIBRARY_TYPE})
# Sources per folder, sorted, each header next to its sources (scripts/check-cmake-lists.py).
target_sources(qc_app_core PRIVATE
    admin/QcAdminController.h admin/QcAdminController.cpp
    admin/QcAdminLineModel.h admin/QcAdminLineModel.cpp
    admin/QcAdminSettingNames.h admin/QcAdminSettingNames.cpp
    admin/QcAdminSettingsDraft.h admin/QcAdminSettingsDraft.cpp
    admin/QcAdminViews.h admin/QcAdminViews.cpp
)
target_sources(qc_app_core PRIVATE
    controllers/QcAnvilController.h controllers/QcAnvilController.cpp
    controllers/QcAudioSettings.h controllers/QcAudioSettings.cpp
    controllers/QcCharacterSettings.h controllers/QcCharacterSettings.cpp
    controllers/QcChatFeed.h controllers/QcChatFeed.cpp
    controllers/QcDiagnosticsSettings.h controllers/QcDiagnosticsSettings.cpp
    controllers/QcEnchantingController.h controllers/QcEnchantingController.cpp
    controllers/QcGameController.h controllers/QcGameController.cpp
    controllers/QcGamepadOptions.h controllers/QcGamepadOptions.cpp
    controllers/QcHandbookController.h controllers/QcHandbookController.cpp
    controllers/QcHeldMap.h controllers/QcHeldMap.cpp
    controllers/QcHudState.h controllers/QcHudState.cpp
    controllers/QcMultiplayerController.h controllers/QcMultiplayerController.cpp
    controllers/QcPhotoMode.h controllers/QcPhotoMode.cpp
    controllers/QcPlayerController.h controllers/QcPlayerController.cpp
    controllers/QcRenderSettings.h controllers/QcRenderSettings.cpp
    controllers/QcScreenState.h controllers/QcScreenState.cpp
    controllers/QcSettingsController.h controllers/QcSettingsController.cpp
    controllers/QcSignController.h controllers/QcSignController.cpp
    controllers/QcStonecutterController.h controllers/QcStonecutterController.cpp
    controllers/QcTexturePackController.h controllers/QcTexturePackController.cpp
    controllers/QcTradeController.h controllers/QcTradeController.cpp
    controllers/QcWorldLibraryController.h controllers/QcWorldLibraryController.cpp
    controllers/QcWorldPause.h controllers/QcWorldPause.cpp
)
target_sources(qc_app_core PRIVATE
    debug/QcAppLogging.h debug/QcAppLogging.cpp
    debug/QcDebugHud.h debug/QcDebugHud.cpp
    debug/QcDebugSettings.h debug/QcDebugSettings.cpp
    debug/QcDebugVisuals.h debug/QcDebugVisuals.cpp
    debug/QcGraphItem.h debug/QcGraphItem.cpp
)
target_sources(qc_app_core PRIVATE
    display/QcDisplaySettings.h display/QcDisplaySettings.cpp
    display/QcFrameLimiter.h display/QcFrameLimiter.cpp
    display/QcWindowPlacement.h display/QcWindowPlacement.cpp
)
target_sources(qc_app_core PRIVATE
    hooks/QcAccessibilityCheck.h hooks/QcAccessibilityCheck.cpp
    hooks/QcBenchmarkRun.h hooks/QcBenchmarkRun.cpp
    hooks/QcFrameMonitor.h hooks/QcFrameMonitor.cpp
    hooks/QcFrameProfile.h hooks/QcFrameProfile.cpp
    hooks/QcLayoutCheck.h hooks/QcLayoutCheck.cpp
    hooks/QcOffscreenRender.h hooks/QcOffscreenRender.cpp
    hooks/QcPackSwitchHitch.h hooks/QcPackSwitchHitch.cpp
    hooks/QcPendingScreens.h hooks/QcPendingScreens.cpp
    hooks/QcScreenshotHook.h hooks/QcScreenshotHook.cpp
    hooks/QcScriptedPad.h hooks/QcScriptedPad.cpp
    hooks/QcStartupHooks.h hooks/QcStartupHooks.cpp
)
target_sources(qc_app_core PRIVATE
    input/QcFocusCandidates.h input/QcFocusCandidates.cpp
    input/QcGameInput.h input/QcGameInput.cpp
    input/QcGamepadBackend.h input/QcGamepadBackend.cpp
    input/QcGamepadRoster.h input/QcGamepadRoster.cpp
    input/QcGamepadUi.h input/QcGamepadUi.cpp
    input/QcKeyControlsForeign.h
    input/QcMenuNavigator.h input/QcMenuNavigator.cpp
    input/QcRelativeMouse.h
)
target_sources(qc_app_core PRIVATE
    models/QcControlPalette.h models/QcControlPalette.cpp
    models/QcCreativeItemModel.h models/QcCreativeItemModel.cpp
    models/QcEnchantmentTexts.h models/QcEnchantmentTexts.cpp
    models/QcHandbookModel.h models/QcHandbookModel.cpp
    models/QcInventoryModel.h models/QcInventoryModel.cpp
    models/QcItemIconProvider.h models/QcItemIconProvider.cpp
    models/QcItemIcons.h models/QcItemIcons.cpp
    models/QcItemIconUrls.h models/QcItemIconUrls.cpp
    models/QcMapImageProvider.h models/QcMapImageProvider.cpp
    models/QcPixelArtProvider.h models/QcPixelArtProvider.cpp
    models/QcSlotRange.h models/QcSlotRange.cpp
    models/QcTexturePackPreviewProvider.h models/QcTexturePackPreviewProvider.cpp
    models/QcUiScaling.h models/QcUiScaling.cpp
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
    rendering/QcBloomChain.h rendering/QcBloomChain.cpp
    rendering/QcBufferWrite.h
    rendering/QcDebugLineRenderer.h rendering/QcDebugLineRenderer.cpp
    rendering/QcDrawOrigins.h rendering/QcDrawOrigins.cpp
    rendering/QcEntityFrameBuilder.h rendering/QcEntityFrameBuilder.cpp
    rendering/QcFramedMapTexture.h rendering/QcFramedMapTexture.cpp
    rendering/QcFramePlan.h
    rendering/QcGpuMeshStore.h rendering/QcGpuMeshStore.cpp
    rendering/QcPostProcess.h rendering/QcPostProcess.cpp
    rendering/QcPrecipitationRenderer.h rendering/QcPrecipitationRenderer.cpp
    rendering/QcRangeAllocator.h rendering/QcRangeAllocator.cpp
    rendering/QcRenderBridge.h rendering/QcRenderBridge.cpp
    rendering/QcRenderPipelines.h rendering/QcRenderPipelines.cpp
    rendering/QcRenderSample.h
    rendering/QcSectionArena.h rendering/QcSectionArena.cpp
    rendering/QcSectionLightPool.h rendering/QcSectionLightPool.cpp
    rendering/QcSkyRenderer.h rendering/QcSkyRenderer.cpp
    rendering/QcWorldRenderer.h rendering/QcWorldRenderer.cpp
    rendering/QcWorldRendererResources.h rendering/QcWorldRendererResources.cpp
    rendering/QcWorldView.h rendering/QcWorldView.cpp
)
target_sources(qc_app_core PRIVATE
    session/QcFrameDriver.h session/QcFrameDriver.cpp
    session/QcSessionAudio.h session/QcSessionAudio.cpp
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
# Section buffers are written straight into Metal buffers (decision E-813); elsewhere through QRhi.
if(APPLE)
    target_sources(qc_app_core PRIVATE rendering/QcBufferWrite_mac.mm)
    target_link_libraries(qc_app_core PRIVATE "-framework Metal")
else()
    target_sources(qc_app_core PRIVATE rendering/QcBufferWrite_other.cpp)
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
    qc_app_core_headers qc_common qc_worldgen qc_worlds qc_clientcore qc_audio
    Qt::Gui Qt::GuiPrivate Qt::Quick Qt::QuickControls2)
# The accessibility check of screenshot runs reads the Accessible attached properties
# (QcAccessibilityCheck, E-725). The public QAccessible::queryAccessibleInterface() is no substitute
# in Qt 6.8: without an active platform accessibility (offscreen runs, no screen reader) it returns
# no interface, and after QAccessible::setActive(true) it reports every item invisible with an
# empty name; Accessible.ignored is not exposed publicly at all.
target_link_libraries(qc_app_core PRIVATE Qt::QuickPrivate)

# The sizes of the UI at the player's scale (decision E-725), one instance for every screen.
set_source_files_properties(qml/controls/Ui.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(qc_app_core
    URI QtCraft
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/QtCraft
    QML_FILES
        qml/Main.qml
        qml/admin/AdminActionDialog.qml
        qml/admin/AdminChatPage.qml
        qml/admin/AdminCommandsPage.qml
        qml/admin/AdminLineList.qml
        qml/admin/AdminListsPage.qml
        qml/admin/AdminLogPage.qml
        qml/admin/AdminOverviewPage.qml
        qml/admin/AdminPlayersPage.qml
        qml/admin/AdminScreen.qml
        qml/admin/AdminServerPage.qml
        qml/admin/AdminSettingRow.qml
        qml/admin/AdminSettingsPage.qml
        qml/character/CharacterEditor.qml
        qml/character/CharacterPresets.qml
        qml/character/CharacterPreviewHost.qml
        qml/character/CharacterPreviewPanel.qml
        qml/character/CharacterScreen.qml
        qml/controls/Bevel.qml
        qml/controls/ControlSlot.qml
        qml/controls/FocusOutline.qml
        qml/controls/GameButton.qml
        qml/controls/GameLogo.qml
        qml/controls/GameSwitch.qml
        qml/controls/GameTextField.qml
        qml/controls/Hint.qml
        qml/controls/ItemSlot.qml
        qml/controls/KeyBadge.qml
        qml/controls/LazyScreen.qml
        qml/controls/LineChart.qml
        qml/controls/ListRow.qml
        qml/controls/ModalDialog.qml
        qml/controls/NavButton.qml
        qml/controls/NavList.qml
        qml/controls/ScreenFrame.qml
        qml/controls/Segments.qml
        qml/controls/SettingCheck.qml
        qml/controls/SettingChoice.qml
        qml/controls/SettingDropdown.qml
        qml/controls/SettingGroup.qml
        qml/controls/SettingRow.qml
        qml/controls/SettingSlider.qml
        qml/controls/SettingSwitch.qml
        qml/controls/SideBar.qml
        qml/controls/SidebarScreen.qml
        qml/controls/ThemedComboBox.qml
        qml/controls/ThemedScrollBar.qml
        qml/controls/ThemedToolTip.qml
        qml/controls/Ui.qml
        qml/controls/UnsavedChangesDialog.qml
        qml/controls/WindowFont.qml
        qml/gamepad/FocusRing.qml
        qml/gamepad/HotbarRadial.qml
        qml/gamepad/OnScreenKeyboard.qml
        qml/gamepad/PadGlyph.qml
        qml/gamepad/PadHintRow.qml
        qml/gamepad/PadHints.qml
        qml/gamepad/PadNotice.qml
        qml/handbook/HandbookEntryRow.qml
        qml/handbook/HandbookLink.qml
        qml/handbook/HandbookPage.qml
        qml/handbook/HandbookRecipe.qml
        qml/handbook/HandbookScreen.qml
        qml/hud/ChatOverlay.qml
        qml/hud/DebugOverlay.qml
        qml/hud/DialDisplay.qml
        qml/hud/HeldItemName.qml
        qml/hud/Hud.qml
        qml/hud/HudLabel.qml
        qml/hud/MapInHand.qml
        qml/hud/PhotoNotice.qml
        qml/hud/PlayerListOverlay.qml
        qml/hud/SignEditor.qml
        qml/hud/WorldLabels.qml
        qml/inventory/AnvilScreen.qml
        qml/inventory/ChestScreen.qml
        qml/inventory/CreativeScreen.qml
        qml/inventory/EnchantingOffer.qml
        qml/inventory/EnchantingScreen.qml
        qml/inventory/FurnaceScreen.qml
        qml/inventory/InventoryScreen.qml
        qml/inventory/InventorySlot.qml
        qml/inventory/LevelBadge.qml
        qml/inventory/MachineSlotRow.qml
        qml/inventory/ParchmentField.qml
        qml/inventory/PixelArt.qml
        qml/inventory/PlayerSlots.qml
        qml/inventory/SlotScreen.qml
        qml/inventory/StonecutterScreen.qml
        qml/inventory/TradeScreen.qml
        qml/inventory/WorkbenchTitle.qml
        qml/menus/CharacterPage.qml
        qml/menus/ContinueCard.qml
        qml/menus/DeathScreen.qml
        qml/menus/DeleteWorldDialog.qml
        qml/menus/DisconnectScreen.qml
        qml/menus/HomePage.qml
        qml/menus/LanScreen.qml
        qml/menus/MainMenu.qml
        qml/menus/MenuSidebar.qml
        qml/menus/MessageScreen.qml
        qml/menus/ModsPage.qml
        qml/menus/MultiplayerPage.qml
        qml/menus/NewWorldPage.qml
        qml/menus/PauseMenu.qml
        qml/menus/ProfileCard.qml
        qml/menus/RecentServerRow.qml
        qml/menus/WorldRow.qml
        qml/menus/WorldTile.qml
        qml/settings/AdvancedPage.qml
        qml/settings/AudioPage.qml
        qml/settings/ControlsPage.qml
        qml/settings/DiagnosticsSection.qml
        qml/settings/GamepadBindingRow.qml
        qml/settings/GamepadPage.qml
        qml/settings/GamepadValues.qml
        qml/settings/GraphicsPage.qml
        qml/settings/InterfacePage.qml
        qml/settings/SettingsPage.qml
        qml/settings/SettingsScreen.qml
        qml/settings/TexturePackRow.qml
        qml/settings/TexturePacksPage.qml
    # The logo's pixel font (Pixelify Sans, SIL Open Font License 1.1) with its licence.
    RESOURCES
        fonts/OFL.txt
        fonts/PixelifySans.ttf
)
# qmltyperegistrar includes the headers of the QML types by file name only (QTBUG-93443): only its
# generated file sees the folders of those headers, the code includes by path below src/ (E-418).
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/qc_app_core_qmltyperegistrations.cpp
    TARGET_DIRECTORY qc_app_core
    PROPERTIES INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_SOURCE_DIR}/controllers;${CMAKE_CURRENT_SOURCE_DIR}/debug;${CMAKE_CURRENT_SOURCE_DIR}/display;${CMAKE_CURRENT_SOURCE_DIR}/input;${CMAKE_CURRENT_SOURCE_DIR}/models;${CMAKE_CURRENT_SOURCE_DIR}/multiplayer;${CMAKE_CURRENT_SOURCE_DIR}/preview;${CMAKE_CURRENT_SOURCE_DIR}/rendering")

# The QML module QtCraft for a program that loads it. A static backing library needs the module's
# static plugin, which registers the types at start. A shared one registers them itself when it is
# loaded (the module registration of qmltyperegistrar), and the plugin is a loadable module that
# nothing links; the engine skips it, because qmldir marks it optional (E-890).
add_library(qc_app_qml INTERFACE)
target_link_libraries(qc_app_qml INTERFACE qc_app_core)
if(NOT QC_SHARED_LIBS)
    target_link_libraries(qc_app_qml INTERFACE qc_app_coreplugin)
endif()

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
else()
    # Windows and Linux: the same icon for the title bar and the taskbar (QcAppWindow::applyWindowIcon),
    # on Windows also embedded into the .exe for Explorer (decision E-875).
    set(QC_ICO ${PROJECT_SOURCE_DIR}/cmake/windows/QtCraft.ico)
    set_source_files_properties(${QC_ICO} PROPERTIES QT_RESOURCE_ALIAS QtCraft.ico)
    qt_add_resources(qtcraft "qtcraft_icon" PREFIX "/icons" FILES ${QC_ICO})
    qc_add_windows_resources(qtcraft "QtCraft")
endif()

target_link_libraries(qtcraft PRIVATE qc_app_qml)

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
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/post.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_down.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_up.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/composite.frag
)
# The first halving of the bloom reads the HDR scene and keeps what exceeds the threshold (decision E-899).
qt_add_shaders(qtcraft "qtcraft_bloom_prefilter_shaders"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}
    PREFIX "/"
    DEFINES QC_PREFILTER=1
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_down.frag
    OUTPUTS
        shaders/bloom_prefilter.frag.qsb
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
# The solid blocks of the opaque pass are drawn without alpha test (decision E-756).
qt_add_shaders(qtcraft "qtcraft_solid_shaders"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}
    PREFIX "/"
    DEFINES QC_OPAQUE=1 QC_SOLID=1
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/block.frag
    OUTPUTS
        shaders/block_solid.frag.qsb
)

qt_add_translations(qtcraft
    SOURCE_TARGETS qtcraft qc_app_core qc_clientcore qc_server qc_worlds qc_network
    TS_FILE_DIR ${PROJECT_SOURCE_DIR}/translations
    TS_FILE_BASE qtcraft
    RESOURCE_PREFIX /i18n)
