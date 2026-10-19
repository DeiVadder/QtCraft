# Checks of the benchmark scripts on canned input; they run no game.
# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test
# properties at the end of that file (labels, costs, required files) apply to these tests.

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
