# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Rendering on the GPU: frame plan, buffers, arena, light, shadows, post-processing, sky, shaders.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

# The material maps of the realistic mode (decision E-908): generator, mip levels, rules, packs.
qc_add_test(test_material_maps SOURCES rendering/test_material_maps.cpp LIBRARIES qc_base_content qc_clientcore TIMEOUT 300)
# block.vert's frame choice in float32 with reciprocal division, and the animation clock (E-878).
qc_add_test(test_texture_animation_shader SOURCES rendering/test_texture_animation_shader.cpp LIBRARIES qc_clientcore)
qc_add_test(test_sky SOURCES rendering/test_sky.cpp LIBRARIES qc_clientcore)
qc_add_test(test_render_culling SOURCES rendering/test_render_culling.cpp LIBRARIES qc_clientcore)
qc_add_test(test_draw_origins SOURCES rendering/test_draw_origins.cpp LIBRARIES qc_app_core)
# The framed maps' bindings have the layout of the entity pipeline's atlas bindings (E-896).
qc_add_test(test_framed_map_texture SOURCES rendering/test_framed_map_texture.cpp LIBRARIES qc_app_core)
qc_add_test(test_label_projector SOURCES rendering/test_label_projector.cpp fixtures/QcTestWorld.h LIBRARIES qc_clientcore)
# The pure parts of the sun shadows (decision E-903): cascades, light axes, texel snapping, light.
qc_add_test(test_shadow_cascades SOURCES rendering/test_shadow_cascades.cpp LIBRARIES qc_clientcore)
# The frame limit on the world's frames: QcFrameDriver::onFrame with a session and a fake clock
# (decision E-835).
qc_add_test(test_frame_driver SOURCES rendering/test_frame_driver.cpp LIBRARIES qc_server qc_app_core qc_worlds)
# The baked shader variants of the game (decision E-332): read from the game's build tree.
qc_add_test(test_shader_variants SOURCES rendering/test_shader_variants.cpp LIBRARIES Qt::Gui Qt::GuiPrivate)
target_compile_definitions(test_shader_variants PRIVATE QTCRAFT_QSB_DIR="${PROJECT_BINARY_DIR}/src/app/.qsb/shaders")
add_dependencies(test_shader_variants qtcraft)
# The Frame uniform block of the baked block, entity and hand shaders: offsets as FrameUniforms
# writes them, identical in the vertex and fragment stage of a pipeline (E-883).
qc_add_test(test_frame_uniforms SOURCES rendering/test_frame_uniforms.cpp LIBRARIES qc_app_core)
target_compile_definitions(test_frame_uniforms PRIVATE QTCRAFT_QSB_DIR="${PROJECT_BINARY_DIR}/src/app/.qsb/shaders")
add_dependencies(test_frame_uniforms qtcraft)
# The per-cell light of the sections (decision E-390): mesher, texels and the GPU pool, uploaded and
# read back on an offscreen QRhi (Metal; skipped without a device).
qc_add_test(test_section_light SOURCES rendering/test_section_light.cpp LIBRARIES qc_app_core)
# The world renderer uploads each section once per frame, however often it changed since the last
# one (decision E-413): rendered through a QQuickRenderControl offscreen (Metal; skipped without a device).
qc_add_test(test_world_renderer_uploads SOURCES rendering/test_world_renderer_uploads.cpp LIBRARIES qc_app_core)
# Section buffers written directly into GPU memory (decision E-813): uploaded through the mesh store
# and read back on an offscreen QRhi (Metal; skipped without a device).
qc_add_test(test_gpu_mesh_upload SOURCES rendering/test_gpu_mesh_upload.cpp LIBRARIES qc_app_core)
# The scene target, bloom and composite of the realistic mode (decision E-899) on QRhi's Null
# backend, with the post-processing shaders baked into the test.
qc_add_test(test_post_process SOURCES rendering/test_post_process.cpp LIBRARIES qc_app_core)
qt_add_shaders(test_post_process "test_post_process_shaders"
    BASE ${PROJECT_SOURCE_DIR}/src/app
    PREFIX "/"
    FILES
        ${PROJECT_SOURCE_DIR}/src/app/shaders/post.vert
        ${PROJECT_SOURCE_DIR}/src/app/shaders/bloom_down.frag
        ${PROJECT_SOURCE_DIR}/src/app/shaders/bloom_up.frag
        ${PROJECT_SOURCE_DIR}/src/app/shaders/composite.frag
        ${PROJECT_SOURCE_DIR}/src/app/shaders/occlusion.frag
)
qt_add_shaders(test_post_process "test_post_process_prefilter_shaders"
    BASE ${PROJECT_SOURCE_DIR}/src/app
    PREFIX "/"
    DEFINES QC_PREFILTER=1
    FILES
        ${PROJECT_SOURCE_DIR}/src/app/shaders/bloom_down.frag
    OUTPUTS
        shaders/bloom_prefilter.frag.qsb
)
# The memory of the material maps (decision E-912): measured with QTCRAFT_MATERIAL_MEASURE=<pack id>.
qc_add_test(test_material_memory SOURCES rendering/test_material_memory.cpp LIBRARIES qc_app_core qc_base_content)
# The material maps on the GPU (decision E-908) on QRhi's Null backend, with the receiver shaders
# baked into the test.
qc_add_test(test_material_textures SOURCES rendering/test_material_textures.cpp LIBRARIES qc_app_core qc_base_content)
qt_add_shaders(test_material_textures "test_material_textures_receiver_shaders"
    BASE ${PROJECT_SOURCE_DIR}/src/app
    PREFIX "/"
    DEFINES QC_SHADOWS=1
    FILES
        ${PROJECT_SOURCE_DIR}/src/app/shaders/entity.vert
        ${PROJECT_SOURCE_DIR}/src/app/shaders/entity.frag
    OUTPUTS
        shaders/entity_shadow.vert.qsb
        shaders/entity_shadow.frag.qsb
)
# The sun's shadow map of the realistic mode (decision E-903) on QRhi's Null backend, with the
# caster shaders baked into the test.
qc_add_test(test_shadow_map SOURCES rendering/test_shadow_map.cpp LIBRARIES qc_app_core)
qt_add_shaders(test_shadow_map "test_shadow_map_shaders"
    BASE ${PROJECT_SOURCE_DIR}/src/app
    PREFIX "/"
    FILES
        ${PROJECT_SOURCE_DIR}/src/app/shaders/shadow.vert
        ${PROJECT_SOURCE_DIR}/src/app/shaders/shadow_entity.vert
        ${PROJECT_SOURCE_DIR}/src/app/shaders/shadow.frag
        ${PROJECT_SOURCE_DIR}/src/app/shaders/entity.vert
        ${PROJECT_SOURCE_DIR}/src/app/shaders/entity.frag
)
qt_add_shaders(test_shadow_map "test_shadow_map_receiver_shaders"
    BASE ${PROJECT_SOURCE_DIR}/src/app
    PREFIX "/"
    DEFINES QC_SHADOWS=1
    FILES
        ${PROJECT_SOURCE_DIR}/src/app/shaders/entity.vert
        ${PROJECT_SOURCE_DIR}/src/app/shaders/entity.frag
    OUTPUTS
        shaders/entity_shadow.vert.qsb
        shaders/entity_shadow.frag.qsb
)
qt_add_shaders(test_shadow_map "test_shadow_map_solid_shaders"
    BASE ${PROJECT_SOURCE_DIR}/src/app
    PREFIX "/"
    DEFINES QC_SOLID=1
    FILES
        ${PROJECT_SOURCE_DIR}/src/app/shaders/shadow.frag
    OUTPUTS
        shaders/shadow_solid.frag.qsb
)
# The water of the realistic mode (decision E-906) on QRhi's Null backend, with its shaders baked
# into the test.
qc_add_test(test_water_surface SOURCES rendering/test_water_surface.cpp LIBRARIES qc_app_core)
qt_add_shaders(test_water_surface "test_water_surface_shaders"
    BASE ${PROJECT_SOURCE_DIR}/src/app
    PREFIX "/"
    FILES
        ${PROJECT_SOURCE_DIR}/src/app/shaders/post.vert
        ${PROJECT_SOURCE_DIR}/src/app/shaders/composite.frag
        ${PROJECT_SOURCE_DIR}/src/app/shaders/water_behind.frag
        ${PROJECT_SOURCE_DIR}/src/app/shaders/water.frag
)
qt_add_shaders(test_water_surface "test_water_surface_receiver_shaders"
    BASE ${PROJECT_SOURCE_DIR}/src/app
    PREFIX "/"
    DEFINES QC_SHADOWS=1
    FILES
        ${PROJECT_SOURCE_DIR}/src/app/shaders/block.vert
        ${PROJECT_SOURCE_DIR}/src/app/shaders/water.frag
    OUTPUTS
        shaders/block_shadow.vert.qsb
        shaders/water_shadow.frag.qsb
)
# The ranges of the section arena's pages (decision E-820): first fit, merging of freed neighbours.
qc_add_test(test_range_allocator SOURCES rendering/test_range_allocator.cpp LIBRARIES qc_app_core)
# The pages of the section arena (decision E-820) on QRhi's Null backend: reuse after the frames in
# flight, oversized sections, rollback, reserve page, per-section layout.
qc_add_test(test_section_arena SOURCES rendering/test_section_arena.cpp LIBRARIES qc_app_core)
# The steps of a frame of the world renderer and their GPU render passes (decision E-390); the plan is
# a header of the app, so the test takes only the app's include folders.
qc_add_test(test_frame_plan SOURCES rendering/test_frame_plan.cpp LIBRARIES qc_app_core_headers Qt::Core)
