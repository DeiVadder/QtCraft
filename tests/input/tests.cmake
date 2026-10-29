# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Input: gamepad, key bindings, mouse, camera.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_camera SOURCES input/test_camera.cpp LIBRARIES qc_clientcore)
qc_add_test(test_mouse_capture SOURCES input/test_mouse_capture.cpp LIBRARIES qc_clientcore)
qc_add_test(test_camera_relative SOURCES input/test_camera_relative.cpp LIBRARIES qc_clientcore)
qc_add_test(test_key_bindings SOURCES input/test_key_bindings.cpp LIBRARIES qc_clientcore)
# The gamepad model (dead zone, curve, types, virtual pads) and the SDL3 backend with its cache
# (decision E-540).
qc_add_test(test_gamepad_input SOURCES input/test_gamepad_input.cpp LIBRARIES qc_clientcore)
# The gamepad controls: walking, looking, triggers, hotbar steps and their settings (decision E-541).
qc_add_test(test_gamepad_mapping SOURCES input/test_gamepad_mapping.cpp LIBRARIES qc_clientcore)
# Profiles per pad model, rebinding, trigger point, button symbols and the mouse's radial menu
# (decisions E-610 to E-614).
qc_add_test(test_gamepad_profiles SOURCES input/test_gamepad_profiles.cpp LIBRARIES qc_clientcore)
# The pad on the screens without a window: spatial focus moves, radial menu, menu commands with
# repeats, and the hotbar selection against older server states (decisions E-581 to E-583).
qc_add_test(test_gamepad_navigation SOURCES input/test_gamepad_navigation.cpp LIBRARIES qc_clientcore)
# The gamepad controls behind the keyboard in the game's frame order: focus, screens, background
# presses, the lead between pads (decision E-541).
qc_add_test(test_gamepad_controls SOURCES input/test_gamepad_controls.cpp LIBRARIES qc_server qc_app_core qc_worlds)
# The pad on the screens of a window: focus moves, clicks, back, tabs, lists, popups, keyboard
# requests and the device in use (decisions E-583, E-584).
qc_add_test(test_gamepad_screens SOURCES input/test_gamepad_screens.cpp LIBRARIES qc_app_core)
# The pad's clicks land on the focused item at a scale factor (positions in device pixels, E-583).
add_test(NAME test_gamepad_screens_scaled COMMAND test_gamepad_screens slotsTakeClicksFromThePad slidersAndSpinBoxesTakeLeftAndRight)
set_tests_properties(test_gamepad_screens_scaled PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1.5;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/test_gamepad_screens_scaled;QTEST_FUNCTION_TIMEOUT=120000")
# The on-screen keyboard: addresses with ports and IPv6, shift, umlauts, hidden passwords (E-584).
qc_add_test(test_onscreen_keyboard SOURCES input/test_onscreen_keyboard.cpp LIBRARIES qc_app_qml)
qc_add_test(test_gamepad_sdl SOURCES input/test_gamepad_sdl.cpp LIBRARIES qc_app_core TIMEOUT 150)
target_compile_definitions(test_gamepad_sdl PRIVATE QTCRAFT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    QTCRAFT_SDL3_DIR="${SDL3_DIR}" QTCRAFT_SDL_ARCH="${QC_SDL_ARCH}" QTCRAFT_CMAKE_COMMAND="${CMAKE_COMMAND}"
    QC_HAVE_SDL_GAMEPAD=$<BOOL:${QC_GAMEPAD_ENABLED}>)
# The test drives SDL's virtual joysticks itself, in the same SDL instance as QcSdlGamepadSource:
# a static qc_app_core leaves SDL to the program, a shared one carries it and exports its API, so
# the test then takes only the headers (a second static copy would be a second SDL, E-890).
if(QC_GAMEPAD_ENABLED AND QC_SHARED_LIBS)
    target_link_libraries(test_gamepad_sdl PRIVATE SDL3::Headers)
elseif(QC_GAMEPAD_ENABLED)
    target_link_libraries(test_gamepad_sdl PRIVATE SDL3::SDL3-static)
endif()
