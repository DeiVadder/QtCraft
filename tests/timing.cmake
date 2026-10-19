# Timing tests (decision E-550): tests whose result depends on wall time or on the load of the
# machine, not only on the code. They measure how punctually timers fire, compare elapsed wall time
# with a bound, wait in real time for other processes or links with latency, or are heavy enough
# that they repeatedly failed only on their time limit while many other tests ran beside them.
#
# They carry the label `timing`. scripts/verify.sh runs every ctest step in two phases: first all
# other tests with QC_TEST_JOBS jobs (`ctest -LE timing`), then these with QC_TIMING_JOBS jobs
# (default 2, `ctest -L timing`) once the machine is quiet (1-min load average below QC_QUIET_LOAD).
# That replaces RUN_SERIAL: the timing phase runs with few jobs and nothing else of the run beside
# it. `--defer-timing` skips the phase (agents working in parallel); the central run before the
# merge runs it. A plain `ctest -j <n>` runs them together with the rest; for the same split run
# `ctest -LE timing -j <n>` and then `ctest -L timing -j 2`.
#
# A new test belongs here when it asserts on QElapsedTimer or tick rates, waits in real time for a
# result, or fails only under parallel load. The label `realtime` (tests/CMakeLists.txt) is separate:
# it lowers the TSan factor of the limit check for tests that mostly wait (scripts/verify-limits.py).
#
#   test_tick_scheduler      measures how punctually the server's tick timer fires (20 ticks in 1 s)
#   test_tick_phases         compares phase times with the measured tick time and ns per lap
#   test_tick_control        /tick sprint and rate in wall time, bounds on messages per real second
#   test_interpolation       walker, watcher and server in real time; samples by the wall clock
#   test_tcp_transport       closing a transport within 100 ms, linger times measured by the clock
#   test_network_game        logins and messages over TCP, ticks paced by wall-time deadlines
#   test_latency             delayed links: round trips and pending inputs bounded by wall time
#   test_creative_client, test_void_client, test_ice_client
#                            client and server host over a delayed link, waiting in real time
#   test_survival_scenario   long scenario waiting in real time for the server host (smelting etc.)
#   test_multiplayer_bots, test_creative_bots, test_signal_bots, test_minecart_bots, test_machine_bots
#                            qtcraft-headless server and bot processes, waits by the clock
#   test_enderman_rules, test_golem_measure, test_taming, test_minecart_momentum, test_signal_showcase
#                            heavy server simulations that repeatedly failed only on their TIMEOUT
#                            under parallel load (golem and minecart measurements time ticks too)
#   app_offscreen_pad_inventory, app_offscreen_pad_radial, app_offscreen_pad_keyboard, app_offscreen_pad_menu,
#   app_offscreen_pad_playstation, app_offscreen_pad_settings, app_offscreen_pad_lazy_menu
#                            the game with a scripted pad: its steps start at wall-clock times of the
#                            startup script (after the world loaded); each waits for rendered frames
#   test_gamepad_scenarios   the creative double tap is timed on the wall clock (E-541)
#   test_session_tick, test_session_thread
#                            the session core ticks on its own thread against real time (E-560)
set(qc_timing_tests
    test_tick_scheduler test_tick_phases test_tick_control test_interpolation
    test_tcp_transport test_network_game test_latency
    test_creative_client test_void_client test_ice_client test_survival_scenario
    test_multiplayer_bots test_creative_bots test_signal_bots test_minecart_bots test_machine_bots
    test_enderman_rules test_golem_measure test_taming test_minecart_momentum test_signal_showcase
    app_offscreen_pad_inventory app_offscreen_pad_radial app_offscreen_pad_keyboard app_offscreen_pad_menu
    app_offscreen_pad_playstation app_offscreen_pad_settings app_offscreen_pad_lazy_menu test_gamepad_scenarios test_session_tick test_session_thread)
set_property(TEST ${qc_timing_tests} APPEND PROPERTY LABELS timing)
