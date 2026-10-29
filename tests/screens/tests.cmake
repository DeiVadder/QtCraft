# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Controllers and models of the screens: admin, advancements, character, chat, inventory, maps, worlds.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_advancement_tree SOURCES screens/test_advancement_tree.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_advancement_tree_model SOURCES screens/test_advancement_tree_model.cpp LIBRARIES qc_app_core qc_worlds)
qc_add_test(test_framed_maps SOURCES screens/test_framed_maps.cpp LIBRARIES qc_base_content qc_clientcore)
# The held map's markers, its image provider and the filled map's tooltip (decision E-805).
qc_add_test(test_map_hud SOURCES screens/test_map_hud.cpp LIBRARIES qc_app_core)
qc_add_test(test_backpack_screen SOURCES screens/test_backpack_screen.cpp LIBRARIES qc_base_content qc_app_core)
# The creative tabs take the handbook's category names (76.2-b).
qc_add_test(test_creative_tabs SOURCES screens/test_creative_tabs.cpp LIBRARIES qc_base_content qc_app_core)
qc_add_test(test_character SOURCES screens/test_character.cpp fixtures/QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_character_preview SOURCES screens/test_character_preview.cpp LIBRARIES qc_base_content qc_clientcore)
# The command list of "Manage server": Tab reaches every row and scrolls it into view (E-911).
qc_add_test(test_admin_command_index SOURCES screens/test_admin_command_index.cpp LIBRARIES qc_app_qml)
# The crosshair text of targeted entities: health only for creatures and players (E-874).
qc_add_test(test_hud_target SOURCES screens/test_hud_target.cpp LIBRARIES qc_base_content qc_app_core)
# The enchanting table's runes, the screens' pixel art and the book and runes in the world (E-884).
qc_add_test(test_rune_script SOURCES screens/test_rune_script.cpp LIBRARIES qc_app_core)
# The enchanting screen's offers in QML: click, Enter, focus, screen reader, level tones (E-884).
qc_add_test(test_enchanting_offer SOURCES screens/test_enchanting_offer.cpp LIBRARIES qc_app_qml)
# The character screens edit a draft: save, discard, close (E-852).
qc_add_test(test_character_draft SOURCES screens/test_character_draft.cpp LIBRARIES qc_app_core qc_worlds)
qc_add_test(test_world_options_controller SOURCES screens/test_world_options_controller.cpp LIBRARIES qc_base_content qc_app_core qc_worlds)
# The map preview of a new world: same map, same as the generated chunks, cancelled by a change (E-921).
qc_add_test(test_world_map_preview SOURCES screens/test_world_map_preview.cpp LIBRARIES qc_base_content qc_app_core qc_worlds TIMEOUT 630)
# The pause menu pauses a world hosted in the game, the handbook from play does not (E-877).
qc_add_test(test_sign_controller SOURCES screens/test_sign_controller.cpp fixtures/QcRawClient.h LIBRARIES qc_app_core qc_worlds qc_server qc_base_content TIMEOUT 150)
# The multiplayer screens (stage 25b, decisions E-258 to E-261): recent servers, LAN addresses, join and leave
# lines and the settings, without a window.
qc_add_test(test_multiplayer_ui SOURCES screens/test_multiplayer_ui.cpp LIBRARIES qc_app_core)
qc_add_test(test_admin_controller SOURCES screens/test_admin_controller.cpp LIBRARIES qc_app_core)
qc_add_test(test_chat_feed SOURCES screens/test_chat_feed.cpp LIBRARIES qc_app_core)
