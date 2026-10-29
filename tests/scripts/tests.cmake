# Checks of the benchmark scripts on canned input; they run no game.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.
# The scripts they check are POSIX shell and Python tools of the Mac and Linux workflow; Windows has
# neither sh nor (by default) python3, so they are not registered there.

if(NOT WIN32)
    # Argument checks of the A/B benchmark script (settings per label, decision E-332); runs no game.
    add_test(NAME script_bench_ab_args
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_bench_ab_args.sh ${PROJECT_SOURCE_DIR}/scripts/bench-ab.sh)
    set_tests_properties(script_bench_ab_args PROPERTIES TIMEOUT 60)
    # The decision of the adaptive benchmark rounds on canned result files (decision E-388); runs no game.
    add_test(NAME script_bench_adaptive
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_bench_adaptive.sh ${PROJECT_SOURCE_DIR}/scripts/bench-adaptive.sh)
    set_tests_properties(script_bench_adaptive PROPERTIES TIMEOUT 60)
    # Test selection of verify.sh --changed by file content (decision E-405) in a throwaway git
    # repository: a touch or a branch switch and back selects nothing; runs no game and no ctest.
    add_test(NAME script_verify_tests_selection
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_verify_tests_selection.py ${PROJECT_SOURCE_DIR}/scripts/verify-tests.py)
    set_tests_properties(script_verify_tests_selection PROPERTIES TIMEOUT 60)
    # The timing phase and the end of a verify.sh run (decisions E-550, E-890, E-913) with a fake load
    # and stubbed ctest and python3: deferral under load, the full-run stamp and its second part, the
    # commands and the final line; runs no build and no ctest.
    add_test(NAME script_verify_timing_phase
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_verify_timing_phase.sh ${PROJECT_SOURCE_DIR}/scripts/verify-phases.sh)
    set_tests_properties(script_verify_timing_phase PROPERTIES TIMEOUT 60)
    # The slimmer full verify (decision E-929): the static analysis selection against a fake stamp
    # commit in a throwaway git repository, and the stamps of a fully green commit and both selections
    # with stubbed python3, ctest and git; runs no build, no ctest and no analysis.
    add_test(NAME script_verify_full_select
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_verify_full_select.py ${PROJECT_SOURCE_DIR}/scripts/verify-full-select.py)
    set_tests_properties(script_verify_full_select PROPERTIES TIMEOUT 60)
    add_test(NAME script_verify_full_stamps
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_verify_full_stamps.sh ${PROJECT_SOURCE_DIR}/scripts/verify-full-select.sh)
    set_tests_properties(script_verify_full_stamps PROPERTIES TIMEOUT 60)
    # The same under dash (the POSIX shell of Linux distributions), when it is installed.
    find_program(QC_DASH dash)
    if(QC_DASH)
        add_test(NAME script_verify_full_stamps_dash
            COMMAND ${QC_DASH} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_verify_full_stamps.sh ${PROJECT_SOURCE_DIR}/scripts/verify-full-select.sh)
        set_tests_properties(script_verify_full_stamps_dash PROPERTIES TIMEOUT 60)
    endif()
    # Samples of the getter/setter check (rule in CLAUDE.md, decision E-891) held in memory; reads no source
    # file below src/, only the check and the helper scripts it imports (REQUIRED_FILES in tests/CMakeLists.txt).
    add_test(NAME script_check_member_access
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_check_member_access.py ${PROJECT_SOURCE_DIR}/scripts/check-member-access.py)
    set_tests_properties(script_check_member_access PROPERTIES TIMEOUT 60)
    # The large file check of the index, a commit and a push (decision E-933) in a throwaway git
    # repository with a bare remote, run from a pre-push hook like tools/githooks/pre-push.
    add_test(NAME script_check_large_files
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_check_large_files.sh ${PROJECT_SOURCE_DIR}/scripts/check-large-files.sh)
    set_tests_properties(script_check_large_files PROPERTIES TIMEOUT 60)
    # Samples of the image reference check of the docs (brace sets, … and <name> placeholders,
    # wildcards; decision E-933), held in memory.
    add_test(NAME script_check_doc_images COMMAND python3 ${PROJECT_SOURCE_DIR}/scripts/check-doc-images.py --self-test)
    set_tests_properties(script_check_doc_images PROPERTIES TIMEOUT 60)
endif()
