# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# User interface in general: building blocks, settings, scale, colours, enums.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_dials SOURCES controls/test_dials.cpp LIBRARIES qc_clientcore)
qc_add_test(test_ui_palette SOURCES controls/test_ui_palette.cpp LIBRARIES qc_clientcore)
target_compile_definitions(test_ui_palette PRIVATE QTCRAFT_QML_DIR="${PROJECT_SOURCE_DIR}/src/app/qml"
                                                  QTCRAFT_THEME_HEADER="${PROJECT_SOURCE_DIR}/src/app/models/QcUiTheme.h")
qc_add_test(test_screen_choice SOURCES controls/test_screen_choice.cpp LIBRARIES qc_clientcore)
qc_add_test(test_settings SOURCES controls/test_settings.cpp LIBRARIES qc_clientcore)
# The display options and the frame pacer on a fake clock (decision E-835).
qc_add_test(test_display_options SOURCES controls/test_display_options.cpp LIBRARIES qc_clientcore)
# The display settings with window modes, the fullscreen key and the frame limit on an offscreen
# window (decision E-835).
qc_add_test(test_display_settings SOURCES controls/test_display_settings.cpp LIBRARIES qc_app_core)
# The settings' controls: Ctrl+Tab (Meta on a Mac) and the pad's shoulders switch the category,
# Enter switches, the arrow keys choose segments (decision E-845).
qc_add_test(test_setting_controls SOURCES controls/test_setting_controls.cpp LIBRARIES qc_app_qml)
# Dialogs that need an answer are modal for the whole window (E-852).
qc_add_test(test_modal_dialog SOURCES controls/test_modal_dialog.cpp LIBRARIES qc_app_qml)
# A shown tooltip has a size and its text, its lines wrap at Ui.size.tooltip (E-874).
qc_add_test(test_themed_tooltip SOURCES controls/test_themed_tooltip.cpp LIBRARIES qc_app_qml)
# The control palette of the windows (decision E-279).
qc_add_test(test_control_palette SOURCES controls/test_control_palette.cpp LIBRARIES qc_app_core)
# The screens that exist only while shown and a while after (LazyScreen.qml, decision E-691),
# loaded from the source tree.
# Enumerations instead of strings for QML and the photo mode's state and files (E-692, E-693).
qc_add_test(test_ui_enums SOURCES controls/test_ui_enums.cpp LIBRARIES qc_app_core qc_worlds)
# The UI scale: steps, Auto, the limit of the window, UiScaling and Ui.px() in QML (E-725).
qc_add_test(test_ui_scale SOURCES controls/test_ui_scale.cpp LIBRARIES qc_app_qml TIMEOUT 150)
qc_add_test(test_lazy_screen SOURCES controls/test_lazy_screen.cpp LIBRARIES Qt::Quick)
target_compile_definitions(test_lazy_screen PRIVATE QTCRAFT_QML_FRAMES_DIR="${PROJECT_SOURCE_DIR}/src/app/qml/frames")
# The graph of the debug display lives in the app; its scene graph nodes need no window.
qc_add_test(test_graph_item SOURCES controls/test_graph_item.cpp LIBRARIES qc_app_core)
