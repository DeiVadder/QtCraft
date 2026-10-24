# Client: textures, meshing, camera, settings, prediction, the app library (qc_app_core) without a window.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_texture_synthesizer SOURCES client/test_texture_synthesizer.cpp LIBRARIES qc_clientcore)
qc_add_test(test_dials SOURCES client/test_dials.cpp LIBRARIES qc_clientcore)
# Maps on the client (decisions E-801, E-802): the cache, the colours from the atlas, the image.
qc_add_test(test_client_maps SOURCES client/test_client_maps.cpp LIBRARIES qc_clientcore)
qc_add_test(test_framed_maps SOURCES client/test_framed_maps.cpp LIBRARIES qc_base_content qc_clientcore)
# The threads between Kjuts and their children (decision E-839).
qc_add_test(test_kjut_threads SOURCES client/test_kjut_threads.cpp LIBRARIES qc_base_content qc_clientcore)
# The held map's markers, its image provider and the filled map's tooltip (decision E-805).
qc_add_test(test_map_hud SOURCES client/test_map_hud.cpp LIBRARIES qc_app_core)
qc_add_test(test_backpack_screen SOURCES client/test_backpack_screen.cpp LIBRARIES qc_app_core)
qc_add_test(test_texture_packs SOURCES client/test_texture_packs.cpp LIBRARIES qc_clientcore)
qc_add_test(test_texture_atlas SOURCES client/test_texture_atlas.cpp LIBRARIES qc_base_content qc_clientcore Qt::Concurrent)
qc_add_test(test_atlas_fill SOURCES client/test_atlas_fill.cpp LIBRARIES qc_base_content qc_clientcore)
# The settings' texture pack controller at model level (decision E-301), from the app's library.
qc_add_test(test_texture_pack_controller SOURCES client/test_texture_pack_controller.cpp LIBRARIES qc_app_core)
# Item icons in QML's pixmap cache (decision E-650): new icons reach shown and newly created images.
qc_add_test(test_item_icon_cache SOURCES client/test_item_icon_cache.cpp LIBRARIES qc_app_core)
# The hitch window of a texture pack switch in a benchmark run (decision E-332), from the app's library.
qc_add_test(test_pack_switch_hitch SOURCES client/test_pack_switch_hitch.cpp LIBRARIES qc_app_core)
qc_add_test(test_camera SOURCES client/test_camera.cpp LIBRARIES qc_clientcore)
qc_add_test(test_mouse_capture SOURCES client/test_mouse_capture.cpp LIBRARIES qc_clientcore)
qc_add_test(test_sky SOURCES client/test_sky.cpp LIBRARIES qc_clientcore)
qc_add_test(test_weather_client SOURCES client/test_weather_client.cpp QcTestWorld.h LIBRARIES qc_clientcore)
# The prediction of the local player on its own (decision E-530): reconciliation, pending inputs, riding.
# The client session's tick thread (decision E-560): pacer and core on a fake clock, long frames of
# the session's thread (real time), order of commands, ending sessions, the fallback without a thread.
qc_add_test(test_session_tick SOURCES client/test_session_tick.cpp QcRecordingTransport.h LIBRARIES qc_server qc_worlds qc_clientcore)
# The session on its tick thread against a server: wish to fly after landing, server stop and kick,
# death and respawn, riding, the same walk as with the timer in the session's thread.
qc_add_test(test_session_thread SOURCES client/test_session_thread.cpp QcRecordingTransport.h LIBRARIES qc_server qc_worlds qc_clientcore)
qc_add_test(test_prediction SOURCES client/test_prediction.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_ui_palette SOURCES client/test_ui_palette.cpp LIBRARIES qc_clientcore)
target_compile_definitions(test_ui_palette PRIVATE QTCRAFT_QML_DIR="${PROJECT_SOURCE_DIR}/src/app/qml"
                                                  QTCRAFT_THEME_HEADER="${PROJECT_SOURCE_DIR}/src/app/models/QcUiTheme.h")
qc_add_test(test_screen_choice SOURCES client/test_screen_choice.cpp LIBRARIES qc_clientcore)
qc_add_test(test_render_culling SOURCES client/test_render_culling.cpp LIBRARIES qc_clientcore)
qc_add_test(test_label_projector SOURCES client/test_label_projector.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_debug_report SOURCES client/test_debug_report.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_app_icon SOURCES client/test_app_icon.cpp LIBRARIES qc_base_content qc_clientcore)
target_compile_definitions(test_app_icon PRIVATE QTCRAFT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
qc_add_test(test_web_handbook SOURCES client/test_web_handbook.cpp LIBRARIES qc_worlds qc_clientcore Qt::Qml)
target_compile_definitions(test_web_handbook PRIVATE QTCRAFT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
qc_add_test(test_mesher SOURCES client/test_mesher.cpp LIBRARIES qc_clientcore)
qc_add_test(test_packed_mesh SOURCES client/test_packed_mesh.cpp LIBRARIES qc_clientcore)
qc_add_test(test_reconciliation SOURCES client/test_reconciliation.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_interpolation SOURCES client/test_interpolation.cpp QcLatencyTransport.h LIBRARIES qc_server qc_worlds qc_clientcore)
qc_add_test(test_mesh_scheduler SOURCES client/test_mesh_scheduler.cpp LIBRARIES qc_clientcore)
qc_add_test(test_block_mirror SOURCES client/test_block_mirror.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_block_prediction SOURCES client/test_block_prediction.cpp QcPredictionFixture.h QcSessionMeshes.h
    LIBRARIES qc_server qc_worlds qc_clientcore)
qc_add_test(test_block_prediction_measure SOURCES client/test_block_prediction_measure.cpp QcPredictionFixture.h QcSessionMeshes.h
    LIBRARIES qc_server qc_worlds qc_clientcore)
qc_add_test(test_settings SOURCES client/test_settings.cpp LIBRARIES qc_clientcore)
qc_add_test(test_character SOURCES client/test_character.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_armour_mesh SOURCES client/test_armour_mesh.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_entity_culling SOURCES client/test_entity_culling.cpp LIBRARIES qc_clientcore)
qc_add_test(test_held_item SOURCES client/test_held_item.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_hold_content SOURCES client/test_hold_content.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_character_preview SOURCES client/test_character_preview.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_cape SOURCES client/test_cape.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_cape_swing SOURCES client/test_cape_swing.cpp LIBRARIES qc_clientcore)
qc_add_test(test_mesher_shapes SOURCES client/test_mesher_shapes.cpp LIBRARIES qc_clientcore)
# Piston heads and diode tops of the base content (decision E-874).
qc_add_test(test_mesher_signal_shapes SOURCES client/test_mesher_signal_shapes.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_block_light_engine SOURCES client/test_block_light_engine.cpp LIBRARIES qc_clientcore TIMEOUT 500)
# Headless measurement of the section meshes (stage 33.2); QTCRAFT_MESH_MEASURE=full for the recorded sizes.
qc_add_test(test_mesh_measure SOURCES client/test_mesh_measure.cpp LIBRARIES qc_base_content qc_worldgen qc_clientcore TIMEOUT 300)
qc_add_test(test_key_bindings SOURCES client/test_key_bindings.cpp LIBRARIES qc_clientcore)
# The display options and the frame pacer on a fake clock (decision E-835).
qc_add_test(test_display_options SOURCES client/test_display_options.cpp LIBRARIES qc_clientcore)
# The display settings with window modes, the fullscreen key and the frame limit on an offscreen
# window (decision E-835).
qc_add_test(test_display_settings SOURCES client/test_display_settings.cpp LIBRARIES qc_app_core)
# The frame limit on the world's frames: QcFrameDriver::onFrame with a session and a fake clock
# (decision E-835).
qc_add_test(test_frame_driver SOURCES client/test_frame_driver.cpp LIBRARIES qc_server qc_app_core qc_worlds)
# The gamepad model (dead zone, curve, types, virtual pads) and the SDL3 backend with its cache
# (decision E-540).
qc_add_test(test_gamepad_input SOURCES client/test_gamepad_input.cpp LIBRARIES qc_clientcore)
# The gamepad controls: walking, looking, triggers, hotbar steps and their settings (decision E-541).
qc_add_test(test_gamepad_mapping SOURCES client/test_gamepad_mapping.cpp LIBRARIES qc_clientcore)
# Profiles per pad model, rebinding, trigger point, button symbols and the mouse's radial menu
# (decisions E-610 to E-614).
qc_add_test(test_gamepad_profiles SOURCES client/test_gamepad_profiles.cpp LIBRARIES qc_clientcore)
# The pad on the screens without a window: spatial focus moves, radial menu, menu commands with
# repeats, and the hotbar selection against older server states (decisions E-581 to E-583).
qc_add_test(test_gamepad_navigation SOURCES client/test_gamepad_navigation.cpp LIBRARIES qc_clientcore)
# The gamepad controls behind the keyboard in the game's frame order: focus, screens, background
# presses, the lead between pads (decision E-541).
qc_add_test(test_gamepad_controls SOURCES client/test_gamepad_controls.cpp LIBRARIES qc_server qc_app_core qc_worlds)
# The session's sounds reach the audio engine each tick (QcSessionAudio, part of QcSessionWiring).
qc_add_test(test_session_audio SOURCES client/test_session_audio.cpp LIBRARIES qc_server qc_app_core qc_worlds)
# The pad on the screens of a window: focus moves, clicks, back, tabs, lists, popups, keyboard
# requests and the device in use (decisions E-583, E-584).
qc_add_test(test_gamepad_screens SOURCES client/test_gamepad_screens.cpp LIBRARIES qc_app_core)
# The pad's clicks land on the focused item at a scale factor (positions in device pixels, E-583).
add_test(NAME test_gamepad_screens_scaled COMMAND test_gamepad_screens slotsTakeClicksFromThePad slidersAndSpinBoxesTakeLeftAndRight)
set_tests_properties(test_gamepad_screens_scaled PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1.5;QTCRAFT_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/test_gamepad_screens_scaled;QTEST_FUNCTION_TIMEOUT=120000")
# The settings' controls: Ctrl+Tab (Meta on a Mac) and the pad's shoulders switch the category,
# Enter switches, the arrow keys choose segments (decision E-845).
qc_add_test(test_setting_controls SOURCES client/test_setting_controls.cpp LIBRARIES qc_app_core qc_app_coreplugin)
# Dialogs that need an answer are modal for the whole window (E-852).
qc_add_test(test_modal_dialog SOURCES client/test_modal_dialog.cpp LIBRARIES qc_app_core qc_app_coreplugin)
# A shown tooltip has a size and its text, its lines wrap at Ui.size.tooltip (E-874).
qc_add_test(test_themed_tooltip SOURCES client/test_themed_tooltip.cpp LIBRARIES qc_app_core qc_app_coreplugin)
# The crosshair text of targeted entities: health only for creatures and players (E-874).
qc_add_test(test_hud_target SOURCES client/test_hud_target.cpp LIBRARIES qc_base_content qc_app_core)
# The on-screen keyboard: addresses with ports and IPv6, shift, umlauts, hidden passwords (E-584).
qc_add_test(test_onscreen_keyboard SOURCES client/test_onscreen_keyboard.cpp LIBRARIES qc_app_core qc_app_coreplugin)
qc_add_test(test_gamepad_sdl SOURCES client/test_gamepad_sdl.cpp LIBRARIES qc_app_core)
target_compile_definitions(test_gamepad_sdl PRIVATE QTCRAFT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    QTCRAFT_SDL3_DIR="${SDL3_DIR}" QTCRAFT_SDL_ARCH="${QC_SDL_ARCH}" QTCRAFT_CMAKE_COMMAND="${CMAKE_COMMAND}"
    QC_HAVE_SDL_GAMEPAD=$<BOOL:${QC_GAMEPAD_ENABLED}>)
# The test drives SDL's virtual joysticks itself.
if(QC_GAMEPAD_ENABLED)
    target_link_libraries(test_gamepad_sdl PRIVATE SDL3::SDL3-static)
endif()
# Field of view while sprinting and the sprint latch (decision E-280).
qc_add_test(test_sprint_feel SOURCES client/test_sprint_feel.cpp LIBRARIES qc_clientcore)
# The control palette of the windows (decision E-279).
qc_add_test(test_control_palette SOURCES client/test_control_palette.cpp LIBRARIES qc_app_core)
# The screens that exist only while shown and a while after (LazyScreen.qml, decision E-691),
# loaded from the source tree.
# Enumerations instead of strings for QML and the photo mode's state and files (E-692, E-693).
qc_add_test(test_ui_enums SOURCES client/test_ui_enums.cpp LIBRARIES qc_app_core qc_worlds)
# The character screens edit a draft: save, discard, close (E-852).
qc_add_test(test_character_draft SOURCES client/test_character_draft.cpp LIBRARIES qc_app_core qc_worlds)
# The pause menu pauses a world hosted in the game, the handbook from play does not (E-877).
qc_add_test(test_world_pause SOURCES client/test_world_pause.cpp QcServerTestFixture.h QcRawClient.h LIBRARIES qc_app_core qc_worlds qc_server)
# The UI scale: steps, Auto, the limit of the window, UiScaling and Ui.px() in QML (E-725).
qc_add_test(test_ui_scale SOURCES client/test_ui_scale.cpp LIBRARIES qc_app_core qc_app_coreplugin)
qc_add_test(test_lazy_screen SOURCES client/test_lazy_screen.cpp LIBRARIES Qt::Quick)
target_compile_definitions(test_lazy_screen PRIVATE QTCRAFT_QML_CONTROLS_DIR="${PROJECT_SOURCE_DIR}/src/app/qml/controls")
# The graph of the debug display lives in the app; its scene graph nodes need no window.
qc_add_test(test_graph_item SOURCES client/test_graph_item.cpp LIBRARIES qc_app_core)
# The baked shader variants of the game (decision E-332): read from the game's build tree.
qc_add_test(test_shader_variants SOURCES client/test_shader_variants.cpp LIBRARIES Qt::Gui Qt::GuiPrivate)
target_compile_definitions(test_shader_variants PRIVATE QTCRAFT_QSB_DIR="${PROJECT_BINARY_DIR}/src/app/.qsb/shaders")
add_dependencies(test_shader_variants qtcraft)
# The shared atlas texture of the character previews, on the Null backend of QRhi.
qc_add_test(test_preview_atlas_cache SOURCES client/test_preview_atlas_cache.cpp LIBRARIES qc_app_core)
# The per-cell light of the sections (decision E-390): mesher, texels and the GPU pool, uploaded and
# read back on an offscreen QRhi (Metal; skipped without a device).
qc_add_test(test_section_light SOURCES client/test_section_light.cpp LIBRARIES qc_app_core)
# The world renderer uploads each section once per frame, however often it changed since the last
# one (decision E-413): rendered through a QQuickRenderControl offscreen (Metal; skipped without a device).
qc_add_test(test_world_renderer_uploads SOURCES client/test_world_renderer_uploads.cpp LIBRARIES qc_app_core)
# Section buffers written directly into GPU memory (decision E-813): uploaded through the mesh store
# and read back on an offscreen QRhi (Metal; skipped without a device).
qc_add_test(test_gpu_mesh_upload SOURCES client/test_gpu_mesh_upload.cpp LIBRARIES qc_app_core)
# The ranges of the section arena's pages (decision E-820): first fit, merging of freed neighbours.
qc_add_test(test_range_allocator SOURCES client/test_range_allocator.cpp LIBRARIES qc_app_core)
# The pages of the section arena (decision E-820) on QRhi's Null backend: reuse after the frames in
# flight, oversized sections, rollback, reserve page, per-section layout.
qc_add_test(test_section_arena SOURCES client/test_section_arena.cpp LIBRARIES qc_app_core)
# The steps of a frame of the world renderer and their GPU render passes (decision E-390); the plan is
# a header of the app, so the test takes only the app's include folders.
qc_add_test(test_frame_plan SOURCES client/test_frame_plan.cpp LIBRARIES qc_app_core_headers Qt::Core)
# The multiplayer screens (stage 25b, decisions E-258 to E-261): recent servers, LAN addresses, join and leave
# lines and the settings, without a window.
qc_add_test(test_multiplayer_ui SOURCES client/test_multiplayer_ui.cpp LIBRARIES qc_app_core)
qc_add_test(test_admin_controller SOURCES client/test_admin_controller.cpp LIBRARIES qc_app_core)
qc_add_test(test_chat_feed SOURCES client/test_chat_feed.cpp LIBRARIES qc_app_core)
