# Diagnostics of a playtest (stage 74, decision E-834): log files, health and lag lines, crash
# reports and diagnostics bundles. Included by tests/CMakeLists.txt (decision E-417): paths are
# relative to tests/, and the test properties at the end of that file apply to these tests.

qc_add_test(test_diagnostics SOURCES diagnostics/test_diagnostics.cpp LIBRARIES qc_server qc_clientcore)
qc_add_test(test_message_log SOURCES diagnostics/test_message_log.cpp LIBRARIES qc_common)
# Crashes qtcraft-headless on purpose and reads the report.
qc_add_test(test_crash_report SOURCES diagnostics/test_crash_report.cpp LIBRARIES qc_common TIMEOUT 300)
target_compile_definitions(test_crash_report PRIVATE QTCRAFT_HEADLESS_PATH="$<TARGET_FILE:qtcraft-headless>")
add_dependencies(test_crash_report qtcraft-headless)
