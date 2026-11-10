# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Runs of the game itself (qtcraft): command line errors, offscreen screenshots and QML load checks.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

# Command line errors of the game itself end it before a window opens.
add_test(NAME app_unknown_world_type_fails
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--world-type;spiral;--screen;builtin;--background;--no-audio"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_ERROR=Unknown world type 'spiral'"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
add_test(NAME app_flat_layers_need_flat_world_fails
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--world-type;void;--flat-layers;bedrock;--screen;builtin;--background;--no-audio"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_ERROR=--flat-layers needs --world-type flat"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
foreach(test app_unknown_world_type_fails app_flat_layers_need_flat_world_fails)
    set_tests_properties(${test} PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/${test}")
endforeach()
# Offscreen rendering (E-229): the platform without windows shows the main menu and the error screen
# without crashing, and a scripted run draws the world without a shown window.
# The menu is drawn through a QRhi, not Qt Quick's software renderer: Metal on macOS (set by
# QcOffscreenRender::prepare), Qt's default backend elsewhere (Direct3D 11 on Windows).
if(APPLE)
    set(qc_offscreen_backend "Metal")
elseif(WIN32)
    set(qc_offscreen_backend "D3D11|D3D12")
else()
    set(qc_offscreen_backend "OpenGL|Vulkan")
endif()
add_test(NAME app_offscreen_platform_menu_screenshot
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--no-audio"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=Rendering offscreen with (${qc_offscreen_backend})"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-menu.png
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
add_test(NAME app_offscreen_platform_error_screen_fails
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--world-type;flat;--flat-layers;bedrock,3*nonsense;--no-audio"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_ERROR=Invalid layer '3\\*nonsense'"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
# Each test writes its own screenshot: the error screen's file is not the one the menu test checks.
foreach(test app_offscreen_platform_menu_screenshot app_offscreen_platform_error_screen_fails)
    set(shot offscreen-menu.png)
    if(test STREQUAL app_offscreen_platform_error_screen_fails)
        set(shot offscreen-error.png)
    endif()
    set_tests_properties(${test} PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/${test};QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/${shot};QTCRAFT_SCREENSHOT_FRAMES=10")
endforeach()
# A picture of the world is far larger than one of the bare HUD over the background (about 50 kB).
add_test(NAME app_offscreen_world_screenshot
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--seed;42;--look;45,-20;--view-distance;4;--no-audio"
        -DEXPECTED_EXIT_CODE=0
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-world.png
        -DEXPECTED_MIN_SIZE=200000
        "-DFORBIDDEN_ERROR=Cannot open shader|Cannot create graphics pipeline|Cannot create entity pipeline"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_world_screenshot PROPERTIES TIMEOUT 180
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_world_screenshot;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-world.png;QTCRAFT_SCREENSHOT_FRAMES=30")
# The full debug display with every visualisation and the debug options page load without QML
# errors, binding loops or other warnings of the QML files (decision E-225).
add_test(NAME app_offscreen_debug_display_qml
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--seed;42;--view-distance;2;--debug-display;full;--debug-show;chunkBorders,hitboxes,lightValues,mobGoals,remeshFlash;--overlay;debug;--no-audio"
        -DEXPECTED_EXIT_CODE=0
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-debug.png
        "-DFORBIDDEN_ERROR=qrc:[^\n]*(Error|Binding loop|Unable to assign|is not a type|Cannot read)"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_debug_display_qml PROPERTIES TIMEOUT 120
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_debug_display_qml;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-debug.png;QTCRAFT_SCREENSHOT_FRAMES=60")
# The character previews (decision E-231): the main menu's profile card, the character screen with
# its presets and the inventory load without QML errors or binding loops, with the --preview hook,
# and the previews of the character screen and the inventory draw the figure.
add_test(NAME app_offscreen_character_preview_qml
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--seed;42;--view-distance;2;--overlay;character;--overlay;@1500 inventory;--preview;animation=mine,side=back,gaze=0.5:0.5,zoom=0.3;--no-audio"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=characterPreview: first figure with [1-9][0-9]+ vertices.*inventoryPreview: first figure with [1-9][0-9]+ vertices"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-preview.png
        "-DFORBIDDEN_ERROR=qrc:[^\n]*(Error|Binding loop|Unable to assign|is not a type|Cannot read|Unable to)"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_character_preview_qml PROPERTIES TIMEOUT 120
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_character_preview_qml;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-preview.png;QTCRAFT_SCREENSHOT_FRAMES=60")
# Armour (decisions E-234, E-238): the inventory with the armour slots, the HUD's armour bar and an
# armour stand with a name load without QML errors, and the inventory's preview draws the armour.
add_test(NAME app_offscreen_armour_qml
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--chat;@200 /equip diamond;--chat;@300 /summon armor_stand equip=iron name=Tin showname;--overlay;@1500 inventory;--no-audio"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=inventoryPreview: first figure with [1-9][0-9]+ vertices"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-armour.png
        "-DFORBIDDEN_ERROR=qrc:[^\n]*(Error|Binding loop|Unable to assign|is not a type|Cannot read|Unable to)"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_armour_qml PROPERTIES TIMEOUT 120
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_armour_qml;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-armour.png;QTCRAFT_SCREENSHOT_FRAMES=90")
add_test(NAME app_preview_hook_refused
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--preview;animation=dance;--screen;builtin;--background;--no-audio"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_ERROR=Invalid --preview setting 'animation=dance'"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_preview_hook_refused PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_preview_hook_refused")
# Offscreen runs are screenshot runs: a benchmark (frame times of the real window) and a run
# without a screenshot (nothing would end it) are refused at once.
add_test(NAME app_offscreen_benchmark_refused
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--no-audio"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_ERROR=Benchmarks measure the real window"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_benchmark_refused PROPERTIES TIMEOUT 60
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_BENCHMARK=5;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_benchmark_refused;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-refused.png")
add_test(NAME app_offscreen_without_screenshot_refused
    COMMAND ${CMAKE_COMMAND} -E env --unset=QTCRAFT_SCREENSHOT --unset=QTCRAFT_BENCHMARK ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--no-audio"
        -DEXPECTED_EXIT_CODE=1
        "-DEXPECTED_ERROR=An offscreen run needs QTCRAFT_SCREENSHOT"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_without_screenshot_refused PROPERTIES TIMEOUT 60
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_without_screenshot_refused")
# QML load test of the character screen in the real game, offscreen (decision E-240). Not under
# sanitizers: the whole game with its render thread is too slow there for the time limit. Not on Windows:
# the layout checks are POSIX shell scripts (sh is no Windows program).
if(NOT QC_SANITIZERS AND NOT WIN32)
    add_test(NAME test_character_screen_qml COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_character_screen_qml.sh $<TARGET_FILE:qtcraft>)
    set_tests_properties(test_character_screen_qml PROPERTIES TIMEOUT 300 COST 40)
    # The loading and start screens at two scales, a refused hook and a real start (decision E-943).
    add_test(NAME app_offscreen_loading_screen COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_loading_screen.sh $<TARGET_FILE:qtcraft>
        $<TARGET_FILE:qtcraft-headless> ${CMAKE_BINARY_DIR}/test-data/app_offscreen_loading_screen)
    # The checks read English texts; QTCRAFT_LANGUAGE keeps them on a system in another language.
    set_tests_properties(app_offscreen_loading_screen PROPERTIES TIMEOUT 600 COST 40
        ENVIRONMENT "QTCRAFT_LANGUAGE=en"
        REQUIRED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ui/test_loading_screen.sh")
    # The main menu and its screens fit the smallest window (1024 x 640): nothing shown lies
    # outside it, the Quit button included.
    add_test(NAME app_offscreen_menu_layout COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_menu_layout.sh $<TARGET_FILE:qtcraft>
        $<TARGET_FILE:qtcraft-headless> layout)
    set_tests_properties(app_offscreen_menu_layout PROPERTIES TIMEOUT 780 COST 37)
    # The pad in the main menu at the same size: the settings' pages, back entries and Y (decision
    # E-925), the handbook, the character's draft, saved worlds and "New world" (decision E-930).
    add_test(NAME app_offscreen_menu_pad COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_menu_layout.sh $<TARGET_FILE:qtcraft>
        $<TARGET_FILE:qtcraft-headless> pad)
    set_tests_properties(app_offscreen_menu_pad PROPERTIES TIMEOUT 780 COST 43)
    # The pause menu's buttons in a saved world and on a joined server, the pause and death cards
    # above the HUD's status rows at 200 % and 225 %, and the sign typed on the pad's keyboard
    # (decision E-928). It waits for the world and a dedicated server in real time: label realtime
    # and properties here (sanitizer builds and Windows have no such test).
    add_test(NAME app_offscreen_pause_screens COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_pause_screens.sh $<TARGET_FILE:qtcraft>
        $<TARGET_FILE:qtcraft-headless> ${CMAKE_BINARY_DIR}/test-data/app_offscreen_pause_screens)
    set_tests_properties(app_offscreen_pause_screens PROPERTIES TIMEOUT 900 COST 60 PROCESSORS 2 LABELS realtime
        REQUIRED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ui/test_pause_screens.sh")
    # The question before a world of an older generator version opens, the switch from it and the
    # progress screen (decisions E-1013, E-1014). It waits for the menu and the world in real time.
    add_test(NAME app_offscreen_generator_upgrade COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_generator_upgrade_dialog.sh
        $<TARGET_FILE:qtcraft> $<TARGET_FILE:qtcraft-headless> ${CMAKE_BINARY_DIR}/test-data/app_offscreen_generator_upgrade)
    set_tests_properties(app_offscreen_generator_upgrade PROPERTIES TIMEOUT 600 COST 60 LABELS realtime
        REQUIRED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ui/test_generator_upgrade_dialog.sh")
    # The HUD while playing: chat lines, a wrapped one included, stay above the hotbar, the
    # experience bar, the level, the hearts, the food and the armour at 1024 x 640 and 1400 x 900;
    # behind screens, full-screen screens, the handbook and the chat line only the parts decision
    # E-927 keeps show, and closing brings them back (1280 x 720 at 100 and 200 %).
    add_test(NAME app_offscreen_hud_layout COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_hud_layout.sh $<TARGET_FILE:qtcraft>)
    set_tests_properties(app_offscreen_hud_layout PROPERTIES TIMEOUT 1080 COST 55)
    # The main menu fills windows up to 3440 x 1440 (a large monitor in fullscreen, decision E-875):
    # Auto lets the page grow with the window, and it stands centred beside the side bar.
    add_test(NAME app_offscreen_window_fill COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_window_fill.sh $<TARGET_FILE:qtcraft>
        ${CMAKE_BINARY_DIR}/test-data/app_offscreen_window_fill)
    set_tests_properties(app_offscreen_window_fill PROPERTIES TIMEOUT 300 COST 20
        REQUIRED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ui/test_window_fill.sh")
    # Every screen and the HUD at 100 % and at 200 % (limited to what fits) in a 1280 x 720 window:
    # nothing outside the window, every control with an accessible name and role (decision E-725).
    foreach(scale 100 200)
        add_test(NAME app_offscreen_ui_scale_${scale} COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_ui_scale.sh $<TARGET_FILE:qtcraft> ${scale}
            ${CMAKE_BINARY_DIR}/test-data/app_offscreen_ui_scale_${scale})
        # Properties here, not at the end of tests/CMakeLists.txt: sanitizer builds have no such test.
        set_tests_properties(app_offscreen_ui_scale_${scale} PROPERTIES TIMEOUT 2310 COST 100
            REQUIRED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ui/test_ui_scale.sh")
    endforeach()
    # The same screens at 100 % in the windows of large monitors at 100 % display scaling (decision
    # E-875): the window's factor grows every screen and the HUD (screens/HUD in percent), nothing
    # lies outside the window.
    foreach(window "1920x1080:113/113" "2560x1440:150/150" "3840x2160:213/213")
        string(REPLACE ":" ";" parts "${window}")
        list(GET parts 0 size)
        list(GET parts 1 expect)
        add_test(NAME app_offscreen_ui_scale_100_${size} COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_ui_scale.sh $<TARGET_FILE:qtcraft> 100
            ${CMAKE_BINARY_DIR}/test-data/app_offscreen_ui_scale_100_${size})
        set_tests_properties(app_offscreen_ui_scale_100_${size} PROPERTIES TIMEOUT 2730 COST 130
            ENVIRONMENT "QC_UI_SIZE=${size};QC_UI_EXPECT=${expect}"
            REQUIRED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ui/test_ui_scale.sh")
    endforeach()
endif()
# The menu's multiplayer page with a refused join (nobody listens on port 1), and a world opened
# to the network on this computer only with the open-to-LAN screen and the player list, load
# without QML errors or binding loops. English texts whatever the system language (the expected
# line is the English one; Windows takes the language of the user's account). The screenshot
# waits for the end of the join (a refusal takes Windows seconds: it repeats the refused connect).
if(NOT QC_SANITIZERS)
    add_test(NAME app_offscreen_multiplayer_menu_qml
        COMMAND ${CMAKE_COMMAND}
            "-DCOMMAND=$<TARGET_FILE:qtcraft>;--overlay;multiplayer;--join;127.0.0.1:1;--player-name;Alex;--no-mods;--no-audio"
            -DEXPECTED_EXIT_CODE=0
            "-DEXPECTED_ERROR=Joining failed: Nobody accepts connections"
            -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-multiplayer.png
            "-DFORBIDDEN_ERROR=qrc:[^\n]*(Error|Binding loop|Unable to assign|is not a type|Cannot read|Unable to)"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
    set_tests_properties(app_offscreen_multiplayer_menu_qml PROPERTIES TIMEOUT 120
        ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_LANGUAGE=en;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_multiplayer_menu_qml;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-multiplayer.png;QTCRAFT_SCREENSHOT_FRAMES=60")
    add_test(NAME app_offscreen_open_to_lan_qml
        COMMAND ${CMAKE_COMMAND}
            "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--host-lan;0;--overlay;@1000 lan;--overlay;@2000 ;--overlay;@2500 players;--no-mods;--no-audio"
            -DEXPECTED_EXIT_CODE=0
            "-DEXPECTED_ERROR=Listening on 127.0.0.1 port"
            -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-lan.png
            "-DFORBIDDEN_ERROR=qrc:[^\n]*(Error|Binding loop|Unable to assign|is not a type|Cannot read|Unable to)"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
    set_tests_properties(app_offscreen_open_to_lan_qml PROPERTIES TIMEOUT 120
        ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_LAN_BIND=127.0.0.1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_open_to_lan_qml;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-lan.png;QTCRAFT_SCREENSHOT_FRAMES=30")
endif()
# The pad on the real screens (decisions E-581 to E-584), played by a scripted virtual pad
# (--overlay "@<ms> pad:<step>"): the log names the focused item after each step. The inventory
# moves the focus and carries a picked-up stack along, the radial menu chooses with the right
# stick, the chat types on the on-screen keyboard and the main menu moves between its buttons;
# each run keeps its screenshot (focus ring, radial menu, keyboard) and loads without QML errors.
set(pad_qml_errors "qrc:[^\n]*(Error|Binding loop|Unable to assign|is not a type|Cannot read|Cannot assign|Unable to)")
add_test(NAME app_offscreen_pad_inventory
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--seed;42;--view-distance;2;--no-audio;--chat;@200 /give stone 5;--overlay;@1000 pad:north;--overlay;@1500 pad:dpadDown;--overlay;@2000 pad:dpadDown;--overlay;@2500 pad:dpadDown;--overlay;@3000 pad:dpadRight;--overlay;@3500 pad:dpadDown;--overlay;@4000 pad:dpadDown;--overlay;@4500 pad:dpadDown;--overlay;@5000 pad:south;--overlay;@5500 pad:dpadUp"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=padFocus: slot:100.*padFocus: slot:103.*padFocus: slot:0.*padFocus: slot:27"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-pad-inventory.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
add_test(NAME app_offscreen_pad_radial
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--seed;42;--view-distance;2;--no-audio;--chat;@200 /give stone 5;--chat;@300 /give oak_planks 7;--chat;@400 /give torch 3;--overlay;@1000 pad:+dpadLeft;--overlay;@1500 pad:rightX=0.9;--overlay;@2000 pad:rightY=0.5"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=padRadial: 0.*padRadial: 3"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-pad-radial.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
add_test(NAME app_offscreen_pad_keyboard
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--seed;42;--view-distance;2;--no-audio;--overlay;@1000 pad:dpadDown;--overlay;@1500 pad:south;--overlay;@2000 pad:dpadRight;--overlay;@2500 pad:south;--overlay;@3000 pad:north;--overlay;@3500 pad:dpadUp;--overlay;@4000 pad:south;--overlay;@4500 pad:west"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=padFocus: key_q.*padKeyboard: qw 2"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-pad-keyboard.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
# The main menu on a first start: down to "Create a new world", left along the first-start links
# (Character, Multiplayer, Handbook) into the side bar, down along its entries.
add_test(NAME app_offscreen_pad_menu
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--no-audio;--overlay;@300 pad:dpadDown;--overlay;@800 pad:dpadLeft;--overlay;@1300 pad:dpadLeft;--overlay;@1800 pad:dpadLeft;--overlay;@2300 pad:dpadLeft;--overlay;@2800 pad:dpadDown"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=padFocus: firstWorldButton.*padFocus: GameButton:Character.*padFocus: GameButton:Multiplayer.*padFocus: GameButton:Handbook.*padFocus: menuEntry[0-9].*padFocus: menuEntry[0-9]"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-pad-menu.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
# The pad's button symbols follow the pad in use (decision E-612): a PlayStation pad connects
# and takes over with a press, the HUD's hints draw its shapes; the gamepad page of the settings
# (decision E-610) lists it with its symbols.
add_test(NAME app_offscreen_pad_playstation
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--seed;42;--view-distance;2;--no-audio;--chat;@200 /give stone 5;--overlay;@1000 pad:connect=playstation;--overlay;@1500 pad:rightShoulder"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=padFamily: playstation"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-pad-playstation.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
add_test(NAME app_offscreen_pad_settings
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--seed;42;--view-distance;2;--no-audio;--overlay;@800 pad:connect=playstation;--overlay;@1200 gamepad;--overlay;@1700 pad:dpadDown;--overlay;@2200 pad:dpadDown"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=padFamily: playstation.*padFocus: "
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-pad-settings.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
# Binding a pad button on the controller page: "Change" of Jump starts the capture, the next button
# (west) is bound to Jump, and the page says which action lost it (row 79.48b: the page used to drop
# its capture before the captured button arrived, so nothing was bound).
add_test(NAME app_offscreen_pad_bind
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--no-audio;--overlay;@800 pad:connect=playstation;--overlay;@1200 gamepad;--overlay;@1700 click:padChange_jump;--overlay;@2200 pad:west"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=Item gamepadMessage: [^\n]* visible yes text [^\n]*now has the button of [^\n]*Jump"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-pad-bind.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
# Screens are unloaded when closed (QTCRAFT_SCREEN_KEEP_ALIVE_MS=0, decision E-691), but the pause
# menu is made in the background while the world plays and kept (decision E-875). The pause card
# (decision E-928) starts on "Continue" at each opening, not on the remembered "Settings", and
# Down reaches "Settings" again.
add_test(NAME app_offscreen_pad_lazy_menu
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--no-audio;--overlay;@1000 pad:start;--overlay;@1500 pad:dpadDown;--overlay;@2000 pad:start;--overlay;@2500 pad:start;--overlay;@3000 pad:dpadDown"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=padFocus: continueGameButton\n.*padFocus: pauseSettingsButton\n.*padFocus: \n.*padFocus: continueGameButton\n.*padFocus: pauseSettingsButton\n"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-pad-lazy_menu.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
# A screen made anew on each opening (QTCRAFT_SCREEN_KEEP_ALIVE_MS=0, decisions E-691, E-875): the
# advancement tree, opened and closed with J, is made twice (QTCRAFT_UI_STATS logs each) and takes
# the pad's focus both times: on its first node, so Right reaches the same child each time.
add_test(NAME app_offscreen_pad_lazy_advancements
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--no-audio;--overlay;@1000 press:J;--overlay;@1500 pad:dpadRight;--overlay;@2000 press:J;--overlay;@2500 press:J;--overlay;@3000 pad:dpadRight"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=AdvancementsScreen[^\n]* made in.*padFocus: advancement_qtcraft_mountain_top[^\n]*\n.*AdvancementsScreen[^\n]* made in.*padFocus: advancement_qtcraft_mountain_top"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-pad-lazy_advancements.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
# The advancement tree (decision E-924): the pad walks along the tree, right to the nearest child,
# down in the column, left back to the parent. The steps run one after another (QcScriptedPad), no
# wall clock; the first press only shows the focus.
add_test(NAME app_offscreen_pad_advancements
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--no-audio;--overlay;advancements;--overlay;@0 pad:dpadRight;--overlay;@0 pad:dpadRight;--overlay;@0 pad:dpadDown;--overlay;@0 pad:dpadLeft"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=padFocus: advancement_qtcraft_adventure[^\n]*\n.*padFocus: advancement_qtcraft_mountain_top[^\n]*\n.*padFocus: advancement_qtcraft_soul_anchor[^\n]*\n.*padFocus: advancement_qtcraft_adventure"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-pad-advancements.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
# Closing the advancement tree (decision E-924) with real key presses (--overlay "@<ms> press:<key>"):
# J opens and closes it from the world; opened from the pause menu, Esc leads back there; J does
# nothing to the screens while the chat line is open (it types into it).
add_test(NAME app_offscreen_advancements_return
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--no-audio;--overlay;@500 press:J;--overlay;@1000 press:J;--overlay;@1500 pause;--overlay;@2000 click:advancementsButton;--overlay;@2500 press:Escape;--overlay;@3000 chat;--overlay;@3500 press:J"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=press:J: overlay advancements[^\n]*\n.*press:J: overlay none[^\n]*\n.*press:Escape: overlay pause[^\n]*\n.*press:J: overlay chat"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-advancements-return.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_advancements_return PROPERTIES TIMEOUT 180
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_advancements_return;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-advancements-return.png;QTCRAFT_SCREENSHOT_FRAMES=30")
# Closing the pause menu's sub-screens (QcScreenState::closeSubScreen, row 79.58) with real key presses:
# Esc on a settings page and on the character screen leads back to the pause menu, Esc on the debug
# options (opened from the world) back to the world; the pad's B on the settings leads back to the
# pause menu, so the last Esc closes it.
add_test(NAME app_offscreen_subscreen_return
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--no-audio;--overlay;@500 audio;--overlay;@1500 press:Escape;--overlay;@2000 debug;--overlay;@3000 press:Escape;--overlay;@3500 character;--overlay;@4500 press:Escape;--overlay;@5000 settings;--overlay;@6000 pad:east;--overlay;@6500 press:Escape"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=press:Escape: overlay pause[^\n]*\n.*press:Escape: overlay none[^\n]*\n.*press:Escape: overlay pause[^\n]*\n.*press:Escape: overlay none"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-subscreen-return.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_subscreen_return PROPERTIES TIMEOUT 180
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_subscreen_return;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-subscreen-return.png;QTCRAFT_SCREENSHOT_FRAMES=30")
# Esc on the HUD's lines (row 79.76): the chat line and a sign's editor (opened by using a sign in
# view) close with a real Esc press, back to the world. Each press waits for its screen
# (press:<key>@<screen>), the use repeats until the placed sign's editor opens (key:use@sign), so
# no step depends on the wall clock.
add_test(NAME app_offscreen_escape_chat_sign
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--no-audio;--look;0,-45;--chat;/setblock ~ ~ ~-1 oak_sign;--overlay;@1000 chat;--overlay;@1000 press:Escape@chat;--overlay;@1500 key:use@sign;--overlay;@1500 press:Escape@sign"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=press:Escape: overlay none \\(was chat\\)[^\n]*\n.*press:Escape: overlay none \\(was sign\\)"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-escape-chat-sign.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_escape_chat_sign PROPERTIES TIMEOUT 180
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_escape_chat_sign;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-escape-chat-sign.png;QTCRAFT_SCREENSHOT_FRAMES=30")
# Arrow up and down in the chat line (row 79.78, E-970): two lines typed and sent with a real
# Return, then on a new draft Up, Up, Down shows the newer one again; the screenshot logs what the
# line shows. Each press waits for the chat line (press:<key>@chat).
add_test(NAME app_offscreen_chat_history
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--no-audio;--overlay;@1000 chat;--overlay;@1300 text:chatInput=first;--overlay;@1300 press:Return@chat;--overlay;@1600 chat;--overlay;@1900 text:chatInput=second;--overlay;@1900 press:Return@chat;--overlay;@2200 chat;--overlay;@2500 text:chatInput=draft;--overlay;@2500 press:Up@chat;--overlay;@2500 press:Up@chat;--overlay;@2500 press:Down@chat"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=press:Return: overlay none \\(was chat\\)[^\n]*\n.*press:Return: overlay none \\(was chat\\).*Item chatInput:[^\n]* text \"second\""
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-chat-history.png
        "-DFORBIDDEN_ERROR=${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_chat_history PROPERTIES TIMEOUT 180
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_chat_history;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-chat-history.png;QTCRAFT_SCREENSHOT_FRAMES=30;QTCRAFT_LOG_ITEMS=chatInput")
foreach(pad inventory radial keyboard menu playstation settings bind lazy_menu lazy_advancements advancements)
    # The world's runs hold the screenshot until the script has run; the menu has no world, so
    # its screenshot waits for frames past the last step.
    set(frames 90)
    set(keep_alive 60000)
    if(pad STREQUAL lazy_menu OR pad STREQUAL lazy_advancements)
        set(keep_alive 0)
    endif()
    if(pad STREQUAL menu)
        set(frames 300)
    endif()
    set_tests_properties(app_offscreen_pad_${pad} PROPERTIES TIMEOUT 180
        ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_LANGUAGE=en;QTCRAFT_SCREEN_KEEP_ALIVE_MS=${keep_alive};QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_pad_${pad};QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-pad-${pad}.png;QTCRAFT_SCREENSHOT_FRAMES=${frames}")
endforeach()
set_property(TEST app_offscreen_pad_bind APPEND PROPERTY ENVIRONMENT "QTCRAFT_LOG_ITEMS=gamepadMessage")
set_property(TEST app_offscreen_pad_lazy_advancements APPEND PROPERTY ENVIRONMENT "QTCRAFT_UI_STATS=1")
# The startup script's steps on a control wait for its screen to be made (decision E-691): the
# search text is set before the creative inventory opens and is typed once it is there.
add_test(NAME app_offscreen_lazy_screen_hooks
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--no-audio;--chat;/gamemode creative;--overlay;@800 text:creativeSearch=stone;--overlay;@900 creative"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=text:creativeSearch=stone\" : waited [0-9]+ ms for its screen"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-lazy-hooks.png
        "-DFORBIDDEN_ERROR=no such control|${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_lazy_screen_hooks PROPERTIES TIMEOUT 120
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_SCREEN_KEEP_ALIVE_MS=0;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_lazy_screen_hooks;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-lazy-hooks.png;QTCRAFT_SCREENSHOT_FRAMES=30")
# Photo mode (decision E-692): its keys hide the HUD and save exactly one screenshot below the data
# directory (emptied first), offscreen through the grab of the screenshot hook.
add_test(NAME app_offscreen_photo_mode
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--no-audio;--overlay;@500 key:hideHud;--overlay;@900 key:screenshot"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=Photo mode: HUD hidden.*Screenshot saved to [^\n]*/app_offscreen_photo_mode/screenshots/[0-9_.-]+\\.png"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-photo.png
        -DCLEAN_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_photo_mode/screenshots
        -DEXPECTED_GLOB=${CMAKE_BINARY_DIR}/test-data/app_offscreen_photo_mode/screenshots/*.png
        -DEXPECTED_GLOB_COUNT=1
        "-DFORBIDDEN_ERROR=Cannot save|${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_photo_mode PROPERTIES TIMEOUT 120
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_photo_mode;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-photo.png;QTCRAFT_SCREENSHOT_FRAMES=30")
# Hiding and showing the world view in a loaded world (as the phase binding of Main.qml does) keeps
# its renderer and the sections on the GPU: no second renderer, no remeshing (decision E-710).
add_test(NAME app_offscreen_view_hidden_keeps_sections
    COMMAND ${CMAKE_COMMAND}
        "-DCOMMAND=$<TARGET_FILE:qtcraft>;--play;--world-type;flat;--view-distance;2;--no-audio;--overlay;@800 view:hide;--overlay;@1400 view:show"
        -DEXPECTED_EXIT_CODE=0
        "-DEXPECTED_ERROR=World view: renderer 1 created.*World view hidden \\([1-9][0-9]* sections\\).*World view shown \\([1-9]"
        -DEXPECTED_FILE=${CMAKE_CURRENT_BINARY_DIR}/offscreen-view-hidden.png
        "-DFORBIDDEN_ERROR=renderer [2-9] created|the GPU lost|no world view|${pad_qml_errors}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake)
set_tests_properties(app_offscreen_view_hidden_keeps_sections PROPERTIES TIMEOUT 120
    ENVIRONMENT "QTCRAFT_OFFSCREEN=1;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/app_offscreen_view_hidden_keeps_sections;QTCRAFT_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-view-hidden.png;QTCRAFT_SCREENSHOT_FRAMES=30")
# "Manage server" in every section, German and English, small and medium window (decision E-863). A POSIX shell script,
# not on Windows.
if(NOT QC_SANITIZERS AND NOT WIN32)
    add_test(NAME app_offscreen_admin_layout COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_admin_layout.sh $<TARGET_FILE:qtcraft>
        ${CMAKE_BINARY_DIR}/test-data/app_offscreen_admin_layout)
    set_tests_properties(app_offscreen_admin_layout PROPERTIES TIMEOUT 750 COST 60
        REQUIRED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ui/test_admin_layout.sh")
endif()
# qtcraft is a GUI program on Windows (WIN32_EXECUTABLE): without a console, Qt sends its messages to
# the debugger (OutputDebugString) instead of standard error, and the checks above would see none.
if(WIN32)
    get_property(qc_ui_tests DIRECTORY PROPERTY TESTS)
    list(FILTER qc_ui_tests INCLUDE REGEX "^(app_|test_character_screen_qml$)")
    set_property(TEST ${qc_ui_tests} APPEND PROPERTY ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
endif()
