qt_add_executable(qtcraft
    main.cpp
    QcScreenshotHook.h QcScreenshotHook.cpp
    QcGameController.h QcGameController.cpp
    QcInventoryModel.h QcInventoryModel.cpp
    QcItemIconProvider.h QcItemIconProvider.cpp
    QcWorldRenderer.h QcWorldRenderer.cpp
    QcWorldView.h QcWorldView.cpp
)
qc_configure_target(qtcraft)
target_compile_definitions(qtcraft PRIVATE QTCRAFT_VERSION="${PROJECT_VERSION}")
set_target_properties(qtcraft PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME QtCraft
    MACOSX_BUNDLE_GUI_IDENTIFIER org.qtcraft.QtCraft
    WIN32_EXECUTABLE ON)

target_link_libraries(qtcraft PRIVATE
    qc_common qc_server qc_clientcore
    Qt::Gui Qt::GuiPrivate Qt::Quick Qt::QuickControls2)

qt_add_qml_module(qtcraft
    URI QtCraft
    QML_FILES
        qml/Main.qml
        qml/ChatOverlay.qml
        qml/DeathScreen.qml
        qml/Hud.qml
        qml/InventoryScreen.qml
        qml/ItemSlot.qml
        qml/PauseMenu.qml
        qml/MainMenu.qml
        qml/MessageScreen.qml
)

qt_add_shaders(qtcraft "qtcraft_shaders"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}
    PREFIX "/"
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/block.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/block.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/outline.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/outline.frag
)

qt_add_translations(qtcraft
    TS_FILE_DIR ${PROJECT_SOURCE_DIR}/translations
    TS_FILE_BASE qtcraft
    RESOURCE_PREFIX /i18n)
