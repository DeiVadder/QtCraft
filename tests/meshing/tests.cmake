# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Meshing of chunks and entities, figures.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

# When the HUD names the selected item over the hotbar (decision E-885).
qc_add_test(test_held_item_name SOURCES meshing/test_held_item_name.cpp LIBRARIES qc_app_core)
qc_add_test(test_mesher SOURCES meshing/test_mesher.cpp LIBRARIES qc_clientcore)
# The corner heights of a liquid's surface (decision E-885).
qc_add_test(test_liquid_surface SOURCES meshing/test_liquid_surface.cpp LIBRARIES qc_clientcore)
qc_add_test(test_packed_mesh SOURCES meshing/test_packed_mesh.cpp LIBRARIES qc_clientcore)
qc_add_test(test_mesh_scheduler SOURCES meshing/test_mesh_scheduler.cpp LIBRARIES qc_clientcore)
# The pure parts of the realistic water (decision E-906): eye under water, wrapped time and camera, Water block.
qc_add_test(test_water_look SOURCES meshing/test_water_look.cpp LIBRARIES qc_clientcore)
qc_add_test(test_armour_mesh SOURCES meshing/test_armour_mesh.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_entity_culling SOURCES meshing/test_entity_culling.cpp LIBRARIES qc_clientcore)
qc_add_test(test_hand_swing SOURCES meshing/test_hand_swing.cpp LIBRARIES qc_clientcore)
qc_add_test(test_held_item SOURCES meshing/test_held_item.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_hold_content SOURCES meshing/test_hold_content.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_boat_look SOURCES meshing/test_boat_look.cpp LIBRARIES qc_clientcore)
qc_add_test(test_cape SOURCES meshing/test_cape.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_cape_swing SOURCES meshing/test_cape_swing.cpp LIBRARIES qc_clientcore)
# The walk and the eyes of creatures built from boxes (decision E-888).
qc_add_test(test_box_legs SOURCES meshing/test_box_legs.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_mesher_shapes SOURCES meshing/test_mesher_shapes.cpp fixtures/QcWorldMesh.h LIBRARIES qc_clientcore)
# Piston heads and diode tops of the base content (decision E-874).
qc_add_test(test_mesher_signal_shapes SOURCES meshing/test_mesher_signal_shapes.cpp fixtures/QcWorldMesh.h LIBRARIES qc_base_content qc_clientcore)
# Headless measurement of the section meshes (stage 33.2); QTCRAFT_MESH_MEASURE=full for the recorded sizes.
qc_add_test(test_mesh_measure SOURCES meshing/test_mesh_measure.cpp LIBRARIES qc_base_content qc_worldgen qc_clientcore TIMEOUT 300)
