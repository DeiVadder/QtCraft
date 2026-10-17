# Client: textures, meshing, camera, settings, prediction, the app library (qc_app_core) without a window.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_texture_synthesizer SOURCES client/test_texture_synthesizer.cpp LIBRARIES qc_clientcore)
qc_add_test(test_texture_packs SOURCES client/test_texture_packs.cpp LIBRARIES qc_clientcore)
qc_add_test(test_texture_atlas SOURCES client/test_texture_atlas.cpp LIBRARIES qc_base_content qc_clientcore Qt::Concurrent)
qc_add_test(test_atlas_fill SOURCES client/test_atlas_fill.cpp LIBRARIES qc_base_content qc_clientcore)
# The settings' texture pack controller at model level (decision E-301), from the app's library.
qc_add_test(test_texture_pack_controller SOURCES client/test_texture_pack_controller.cpp LIBRARIES qc_app_core)
# The hitch window of a texture pack switch in a benchmark run (decision E-332), from the app's library.
qc_add_test(test_pack_switch_hitch SOURCES client/test_pack_switch_hitch.cpp LIBRARIES qc_app_core)
qc_add_test(test_camera SOURCES client/test_camera.cpp LIBRARIES qc_clientcore)
qc_add_test(test_mouse_capture SOURCES client/test_mouse_capture.cpp LIBRARIES qc_clientcore)
qc_add_test(test_sky SOURCES client/test_sky.cpp LIBRARIES qc_clientcore)
qc_add_test(test_weather_client SOURCES client/test_weather_client.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_ui_palette SOURCES client/test_ui_palette.cpp LIBRARIES qc_clientcore)
target_compile_definitions(test_ui_palette PRIVATE QTCRAFT_QML_DIR="${PROJECT_SOURCE_DIR}/src/app/qml"
                                                  QTCRAFT_THEME_HEADER="${PROJECT_SOURCE_DIR}/src/app/models/QcUiTheme.h")
qc_add_test(test_screen_choice SOURCES client/test_screen_choice.cpp LIBRARIES qc_clientcore)
qc_add_test(test_render_culling SOURCES client/test_render_culling.cpp LIBRARIES qc_clientcore)
qc_add_test(test_label_projector SOURCES client/test_label_projector.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_debug_report SOURCES client/test_debug_report.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_app_icon SOURCES client/test_app_icon.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_web_handbook SOURCES client/test_web_handbook.cpp LIBRARIES qc_server qc_clientcore Qt::Qml)
target_compile_definitions(test_web_handbook PRIVATE QTCRAFT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
qc_add_test(test_mesher SOURCES client/test_mesher.cpp LIBRARIES qc_clientcore)
qc_add_test(test_reconciliation SOURCES client/test_reconciliation.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_interpolation SOURCES client/test_interpolation.cpp QcLatencyTransport.h LIBRARIES qc_server qc_clientcore)
qc_add_test(test_mesh_scheduler SOURCES client/test_mesh_scheduler.cpp LIBRARIES qc_clientcore)
qc_add_test(test_settings SOURCES client/test_settings.cpp LIBRARIES qc_clientcore)
qc_add_test(test_character SOURCES client/test_character.cpp QcTestWorld.h LIBRARIES qc_clientcore)
qc_add_test(test_armour_mesh SOURCES client/test_armour_mesh.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_character_preview SOURCES client/test_character_preview.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_cape SOURCES client/test_cape.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_cape_swing SOURCES client/test_cape_swing.cpp LIBRARIES qc_clientcore)
qc_add_test(test_mesher_shapes SOURCES client/test_mesher_shapes.cpp LIBRARIES qc_clientcore)
qc_add_test(test_block_light_engine SOURCES client/test_block_light_engine.cpp LIBRARIES qc_clientcore TIMEOUT 500)
# Headless measurement of the section meshes (stage 33.2); QTCRAFT_MESH_MEASURE=full for the recorded sizes.
qc_add_test(test_mesh_measure SOURCES client/test_mesh_measure.cpp LIBRARIES qc_base_content qc_worldgen qc_clientcore TIMEOUT 300)
qc_add_test(test_key_bindings SOURCES client/test_key_bindings.cpp LIBRARIES qc_clientcore)
# Field of view while sprinting and the sprint latch (decision E-280).
qc_add_test(test_sprint_feel SOURCES client/test_sprint_feel.cpp LIBRARIES qc_clientcore)
# The control palette of the windows (decision E-279).
qc_add_test(test_control_palette SOURCES client/test_control_palette.cpp LIBRARIES qc_app_core)
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
# The steps of a frame of the world renderer and their GPU render passes (decision E-390); the plan is
# a header of the app, so the test takes only the app's include folders.
qc_add_test(test_frame_plan SOURCES client/test_frame_plan.cpp LIBRARIES qc_app_core_headers Qt::Core)
# The multiplayer screens (stage 25b, decisions E-258 to E-261): recent servers, LAN addresses, join and leave
# lines and the settings, without a window.
qc_add_test(test_multiplayer_ui SOURCES client/test_multiplayer_ui.cpp LIBRARIES qc_app_core)
