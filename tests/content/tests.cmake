# Content: definitions, packs, mods, crafting, handbook and translations.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

qc_add_test(test_content SOURCES content/test_content.cpp LIBRARIES qc_common)
qc_add_test(test_content_pack SOURCES content/test_content_pack.cpp LIBRARIES qc_base_content qc_clientcore)
qc_add_test(test_village_mods SOURCES content/test_village_mods.cpp LIBRARIES qc_base_content)
qc_add_test(test_creative_catalog SOURCES content/test_creative_catalog.cpp LIBRARIES qc_base_content)
qc_add_test(test_crafting SOURCES content/test_crafting.cpp LIBRARIES qc_common)
qc_add_test(test_armour SOURCES content/test_armour.cpp LIBRARIES qc_server)
qc_add_test(test_mods SOURCES content/test_mods.cpp LIBRARIES qc_server qc_worlds TIMEOUT 600)
target_compile_definitions(test_mods PRIVATE QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
# Headless measurement of the translations (stage 43); QTCRAFT_TRANSLATION_MEASURE=full for the recorded sizes.
qc_add_test(test_translation_measure SOURCES content/test_translation_measure.cpp LIBRARIES qc_worlds TIMEOUT 300)
qc_add_test(test_translation_limits SOURCES content/test_translation_limits.cpp LIBRARIES qc_worlds TIMEOUT 300)
qc_add_test(test_bundled_mods SOURCES content/test_bundled_mods.cpp LIBRARIES qc_worlds)
target_compile_definitions(test_bundled_mods PRIVATE QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
qc_add_test(test_cart_content SOURCES content/test_cart_content.cpp LIBRARIES qc_worlds)
qc_add_test(test_sound_content SOURCES content/test_sound_content.cpp LIBRARIES qc_worlds)
target_compile_definitions(test_sound_content PRIVATE QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods")
qc_add_test(test_shapes SOURCES content/test_shapes.cpp QcTestWorld.h LIBRARIES qc_common)
qc_add_test(test_handbook SOURCES content/test_handbook.cpp LIBRARIES qc_worlds)
qc_add_test(test_handbook_files SOURCES content/test_handbook_files.cpp LIBRARIES qc_common)
qc_add_test(test_handbook_tables SOURCES content/test_handbook_tables.cpp LIBRARIES qc_server qc_worlds)
target_compile_definitions(test_handbook PRIVATE QTCRAFT_MODS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/mods"
                                                QTCRAFT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
qc_add_test(test_server_messages SOURCES content/test_server_messages.cpp LIBRARIES Qt::Core)
target_compile_definitions(test_server_messages PRIVATE QTCRAFT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
