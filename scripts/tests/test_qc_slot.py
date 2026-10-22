#!/usr/bin/env python3
"""Tests of the slot broker scripts/qc-slot.py (decision E-781).

Every test runs its own broker in a temporary slot directory (QC_SLOT_DIR and HOME point there), with
a load read from a file (it stands for both the 1-min and the short-term load), 8 cores and a tick of 50 ms, so nothing touches ~/.cache/qtcraft and the
whole file runs in a few seconds. Run: python3 scripts/tests/test_qc_slot.py (verify.sh runs it in
the step check-slot-broker).
"""

import contextlib
import io
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

SCRIPTS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SLOT = os.path.join(SCRIPTS, "qc-slot.py")
WAIT_QUIET = os.path.join(SCRIPTS, "wait-quiet.sh")


def slot_module():
    import importlib.util
    spec = importlib.util.spec_from_file_location("qc_slot", SLOT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FakeClock:
    def __init__(self):
        self.now = 100.0

    def __call__(self):
        return self.now


def wait_until(predicate, seconds=5.0):
    deadline = time.time() + seconds
    while time.time() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.03)
    return predicate()


class SlotBrokerTest(unittest.TestCase):
    def setUp(self):
        base = tempfile.mkdtemp(prefix="qcs")
        if len(base) > 70:
            shutil.rmtree(base)
            base = tempfile.mkdtemp(prefix="qcs", dir="/tmp")
        self.base = base
        self.dir = os.path.join(base, ".cache", "qtcraft")
        os.makedirs(self.dir)
        self.load_file = os.path.join(base, "load")
        self.set_load(0)
        self.env = {k: v for k, v in os.environ.items() if not k.startswith("QC_")}
        self.env.update(HOME=base, QC_SLOT_DIR=self.dir, QC_SLOT_CORES="8", QC_SLOT_TICK="0.05",
                        QC_SLOT_IDLE="30", QC_SLOT_FAKE_LOAD=self.load_file, QC_SLOT_POLL="0.05")
        self.processes = []
        self.server = None

    def tearDown(self):
        for process in self.processes:
            if process.poll() is None:
                process.kill()
            process.wait()
            for stream in (process.stdout, process.stderr):
                if stream:
                    stream.close()
        state = json.loads(self.slot("status", "--json").stdout or "{}")
        if self.slot("stop", "--force").returncode != 0 and state.get("pid"):
            # Refused during a benchmark: this test's own broker ends the hard way.
            os.kill(state["pid"], 9)
        if self.server:
            try:
                self.server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.server.kill()
                self.server.wait()
        shutil.rmtree(self.base, ignore_errors=True)

    # ---- helpers ----

    def set_load(self, value):
        with open(self.load_file, "w", encoding="utf-8") as handle:
            handle.write(str(value))

    def start_server(self, **extra):
        env = {**self.env, **extra}
        self.server = subprocess.Popen([sys.executable, SLOT, "serve"], env=env, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL)
        self.assertTrue(wait_until(lambda: os.path.exists(os.path.join(self.dir, "qc-slot.sock"))))

    def slot(self, *args, env=None):
        return subprocess.run([sys.executable, SLOT, *args], env=env or self.env, capture_output=True, text=True,
                              timeout=20)

    def spawn(self, *args, env=None):
        process = subprocess.Popen([sys.executable, SLOT, *args], env=env or self.env, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        self.processes.append(process)
        return process

    def acquire(self, kind, who, *more):
        if kind == "bench" and "--pid" not in more and "--hold" not in more:
            more = (*more, "--hold", "600")
        return self.spawn("acquire", "--kind", kind, "--who", who, *more)

    def status(self):
        return json.loads(self.slot("status", "--json").stdout)

    def holders(self):
        return {h["who"]: h for h in self.status()["holders"]}

    def queued(self):
        return [e["who"] for e in self.status()["queue"]]

    def granted(self, process):
        out, err = process.communicate(timeout=10)
        self.assertEqual(process.returncode, 0, err)
        exports = dict(line[len("export "):].split("=", 1) for line in out.splitlines() if line.startswith("export "))
        return exports

    def hold(self, kind, who, *more):
        """A granted slot (hold lease) and its exports."""
        exports = self.granted(self.acquire(kind, who, *more))
        self.assertIn(who, self.holders())
        return exports

    def release(self, ident):
        self.assertEqual(self.slot("release", "--id", str(ident)).returncode, 0)

    def make_lock(self, name, pid):
        path = os.path.join(self.dir, name)
        os.mkdir(path)
        with open(os.path.join(path, "owner"), "w", encoding="utf-8") as handle:
            handle.write(f"bench-ab.sh pid={pid} dir=/x result=/y\n" if name == "bench.lock" else f"pid={pid} dir=/x tool=verify.sh\n")
        return path

    def sleeper(self):
        process = subprocess.Popen(["sleep", "30"])
        self.processes.append(process)
        return process

    # ---- tests ----

    def test_blocking_grant_and_job_exports(self):
        self.start_server()
        first = self.hold("verify", "a", "--jobs", "8")
        self.assertEqual(first["QC_BUILD_JOBS"], "8")
        waiting = self.acquire("build", "b")
        time.sleep(0.4)
        self.assertIsNone(waiting.poll(), "build granted although all jobs are in use")
        self.assertEqual(self.queued(), ["b"])
        self.release(first["QC_SLOT_ID"])
        exports = self.granted(waiting)
        self.assertEqual((exports["QC_BUILD_JOBS"], exports["QC_TEST_JOBS"], exports["QC_SLOT_JOBS"]), ("4", "4", "4"))

    def test_fifo_within_kind_and_no_overtaking(self):
        self.start_server()
        first = self.hold("verify", "a", "--jobs", "8")
        b = self.acquire("test", "b", "--jobs", "8")
        wait_until(lambda: self.queued() == ["b"])
        c = self.acquire("test", "c", "--jobs", "2")
        wait_until(lambda: len(self.queued()) == 2)
        self.release(first["QC_SLOT_ID"])
        self.granted(b)
        time.sleep(0.3)
        self.assertIsNone(c.poll(), "a later request overtook one that waits for jobs")
        self.assertEqual(self.queued(), ["c"])

    def test_heavy_not_overtaken_by_later_builds(self):
        self.start_server()
        b1 = self.hold("build", "b1")
        self.hold("build", "b2")
        heavy = self.acquire("verify", "v")
        wait_until(lambda: self.queued() == ["v"])
        b3 = self.acquire("build", "b3")
        wait_until(lambda: len(self.queued()) == 2)
        self.release(b1["QC_SLOT_ID"])
        self.assertEqual(self.granted(heavy)["QC_TEST_JOBS"], "4")
        self.assertIsNone(b3.poll(), "a later build took the jobs a verify waited for")
        self.assertEqual(self.queued(), ["b3"])

    def test_queue_reason_names_the_earlier_request(self):
        self.start_server()
        self.hold("verify", "a", "--jobs", "8")
        self.acquire("verify", "v")
        wait_until(lambda: self.queued() == ["v"])
        self.acquire("build", "b")
        wait_until(lambda: len(self.queued()) == 2)
        self.assertIn("behind #2 verify 'v'", self.status()["queue"][1]["reason"])

    def test_explicit_heavy_wish_needs_half(self):
        self.start_server()
        self.hold("build", "b", "--jobs", "5")
        out = self.slot("acquire", "--kind", "verify", "--who", "big", "--jobs", "8", "--timeout", "0.3")
        self.assertEqual(out.returncode, 1, "a run sized for 8 jobs got 3")
        self.assertEqual(self.hold("verify", "small")["QC_TEST_JOBS"], "3")

    def test_slot_kind_decides_nesting(self):
        self.start_server()
        outer = self.hold("verify", "a", "--jobs", "8")
        inside_heavy = {**self.env, "QC_SLOT_ID": outer["QC_SLOT_ID"], "QC_SLOT_KIND": "verify"}
        out = self.slot("run", "--kind", "build", "--who", "n", "--timeout", "0.3", "--", "true", env=inside_heavy)
        self.assertEqual(out.returncode, 0, "a build inside a verify slot asked the broker")
        inside_build = {**self.env, "QC_SLOT_ID": outer["QC_SLOT_ID"], "QC_SLOT_KIND": "build"}
        out = self.slot("run", "--kind", "verify", "--who", "n", "--timeout", "0.3", "--", "true", env=inside_build)
        self.assertEqual(out.returncode, 2, "a verify inside a build slot was not refused")
        self.assertIn("--kind verify", out.stderr)
        self.assertEqual(self.queued(), [], "a refused nested request was queued")

    def test_nested_bench_refused(self):
        self.start_server()
        outer = self.hold("test", "t", "--jobs", "2")
        inside = {**self.env, "QC_SLOT_ID": outer["QC_SLOT_ID"], "QC_SLOT_KIND": "test"}
        out = self.slot("run", "--kind", "bench", "--who", "b", "--", "true", env=inside)
        self.assertEqual(out.returncode, 2)
        self.assertIn("--kind bench", out.stderr)
        out = self.slot("acquire", "--kind", "bench", "--who", "b", "--hold", "60", env=inside)
        self.assertEqual(out.returncode, 2)
        self.assertEqual(self.queued(), [])

    def test_bench_command_owns_its_lock(self):
        self.start_server()
        script = ('grep -q "pid=$QC_BENCH_LOCK_PID " "$QC_SLOT_DIR/bench.lock/owner" || exit 5; '
                  f'QC_SLOT=0 QC_QUIET_PAUSE_MAX=1 sh "{WAIT_QUIET}" || exit 6; '
                  f'env -u QC_BENCH_LOCK_PID QC_SLOT=0 QC_QUIET_PAUSE_MAX=1 sh "{WAIT_QUIET}" || exit 7')
        out = self.slot("run", "--kind", "bench", "--who", "b", "--", "sh", "-c", script)
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.dir, "bench.lock")), "bench.lock left after the command ended")

    def test_bench_acquire_needs_pid_or_short_hold(self):
        self.start_server()
        self.assertEqual(self.slot("acquire", "--kind", "bench", "--who", "x").returncode, 2)
        self.assertEqual(self.slot("acquire", "--kind", "bench", "--who", "x", "--hold", "60").returncode, 0)

    def test_stop_refused_during_bench_and_lock_kept_while_owner_runs(self):
        self.start_server()
        sleeper = self.sleeper()
        bench = self.hold("bench", "bench", "--pid", str(sleeper.pid))
        out = self.slot("stop", "--force")
        self.assertEqual(out.returncode, 1, "the broker stopped during a benchmark")
        self.release(bench["QC_SLOT_ID"])
        lock = os.path.join(self.dir, "bench.lock")
        self.assertTrue(os.path.isdir(lock), "bench.lock removed while the benchmark still runs")
        sleeper.kill()
        sleeper.wait()
        self.assertEqual(self.hold("bench", "next", "--hold", "60")["QC_SLOT_KIND"], "bench")

    def test_run_slot_survives_broker_restart(self):
        self.start_server()
        flag = os.path.join(self.base, "go")
        runner = self.spawn("run", "--kind", "verify", "--who", "a", "--", "sh", "-c",
                            f'while [ ! -e "{flag}" ]; do sleep 0.05; done')
        wait_until(lambda: "a" in self.holders())
        old = self.status()["pid"]
        old_id = self.holders()["a"]["id"]
        self.assertEqual(self.slot("stop", "--force").returncode, 0)
        self.server.wait(timeout=5)
        adopted = wait_until(lambda: self.slot("status", "--json").returncode == 0
                             and "a" in {h["who"] for h in json.loads(self.slot("status", "--json").stdout).get("holders", [])})
        self.assertTrue(adopted, "the running slot was not registered with the new broker")
        self.assertNotEqual(self.status()["pid"], old)
        self.assertEqual(self.holders()["a"]["id"], old_id, "the adopted slot changed its id")
        open(flag, "w").close()
        self.assertEqual(runner.wait(timeout=5), 0)
        self.assertIn("WARNING: the broker ended", runner.stderr.read())
        self.assertTrue(wait_until(lambda: self.status()["holders"] == []))

    def test_adopt_avoids_queued_ids(self):
        import socket as sockets
        self.start_server()
        self.hold("verify", "a", "--jobs", "8")
        self.acquire("build", "b")
        wait_until(lambda: self.queued() == ["b"])
        queued_id = self.status()["queue"][0]["id"]
        conn = sockets.socket(sockets.AF_UNIX, sockets.SOCK_STREAM)
        conn.connect(os.path.join(self.dir, "qc-slot.sock"))
        conn.sendall((json.dumps({"op": "adopt", "kind": "test", "who": "old", "jobs": 1, "id": queued_id,
                                  "client_pid": os.getpid()}) + "\n").encode())
        answer = json.loads(conn.makefile().readline())
        self.assertNotEqual(answer["id"], queued_id)
        self.assertIn("taken", self.holders()["old"]["note"])
        later = self.granted(self.acquire("light", "l"))
        self.assertGreater(int(later["QC_SLOT_ID"]), answer["id"])
        conn.close()

    def test_job_budget(self):
        self.start_server()
        self.assertEqual(self.hold("verify", "a")["QC_TEST_JOBS"], "4")
        self.assertEqual(self.hold("tsan", "b", "--jobs", "3")["QC_TEST_JOBS"], "3")
        # One job left: below the minimum of a build.
        self.acquire("build", "c")
        wait_until(lambda: self.queued() == ["c"])
        state = self.status()
        self.assertEqual((state["used"], state["budget"]), (7, 8))
        self.assertTrue(state["queue"][0]["reason"].startswith("jobs:"))
        # Light work is outside the budget.
        self.assertEqual(self.hold("light", "d")["QC_SLOT_JOBS"], "1")

    def test_wish_capped_by_free_jobs_and_own_env(self):
        self.start_server()
        self.hold("verify", "a", "--jobs", "6")
        env = {**self.env, "QC_TEST_JOBS": "1"}
        out = self.slot("run", "--kind", "test", "--who", "r", "--jobs", "4", "--", "sh", "-c",
                        "echo $QC_BUILD_JOBS $QC_TEST_JOBS", env=env)
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertEqual(out.stdout.strip(), "2 1")

    def test_bench_exclusive_and_not_starved(self):
        self.start_server()
        heavy = self.hold("verify", "a")
        bench = self.acquire("bench", "bench")
        wait_until(lambda: self.queued() == ["bench"])
        later = self.acquire("build", "late")
        wait_until(lambda: len(self.queued()) == 2)
        time.sleep(0.3)
        self.assertIsNone(later.poll(), "a build passed a waiting benchmark")
        self.assertIsNone(bench.poll(), "benchmark granted beside a verify slot")
        self.release(heavy["QC_SLOT_ID"])
        exports = self.granted(bench)
        lock = os.path.join(self.dir, "bench.lock")
        with open(os.path.join(lock, "owner"), encoding="utf-8") as handle:
            owner = handle.read()
        self.assertRegex(owner, r"^bench-[a-z]*\.sh pid=\d+ ")
        self.assertIn("QC_BENCH_LOCK_PID", exports)
        time.sleep(0.2)
        self.assertIsNone(later.poll(), "a build granted during a benchmark")
        self.release(exports["QC_SLOT_ID"])
        self.granted(later)
        self.assertFalse(os.path.exists(lock), "bench.lock left after the benchmark")

    def write_history(self, *slots):
        """Release lines of earlier slots in the broker log: (kind, who, seconds)."""
        with open(os.path.join(self.dir, "qc-slot.log"), "a", encoding="utf-8") as handle:
            for number, (kind, who, seconds) in enumerate(slots, 1):
                handle.write(f"2026-10-22 07:00:00 release #{number} {kind} who={who!r} after {seconds}s: released\n")

    def bench_behind_verify(self, **extra):
        """A broker with a held verify 'a' (usually 600 s) and a benchmark waiting for it."""
        self.write_history(("verify", "a", 600), ("build", "t1 short", 5), ("build", "t1 short", 3),
                           ("build", "t1 long", 3000), ("build", "t1 long", 20), ("build", "t1 once", 1),
                           ("verify", "v2", 5), ("verify", "v2", 5), ("bench", "bench", 120))
        self.start_server(**extra)
        heavy = self.hold("verify", "a")
        bench = self.acquire("bench", "bench")
        wait_until(lambda: self.queued() == ["bench"])
        return heavy, bench

    def test_backfill_short_build_while_bench_waits(self):
        heavy, bench = self.bench_behind_verify()
        short = self.hold("build", "t1 short")
        self.assertIn("backfill", self.holders()["t1 short"].get("note", ""))
        self.assertIn("backfill_end", self.holders()["t1 short"])
        self.assertIn("1 of them backfill", self.status()["queue"][0]["reason"])
        self.assertIn("eta", self.status()["queue"][0], "no ETA for the bench although every holder has history")
        for who, why in (("t1 long", "takes up to"), ("t2 unknown", "fewer than 2 past durations"),
                         ("t1 once", "fewer than 2 past durations"), ("v2", "no new verify")):
            kind = "verify" if who == "v2" else "build"
            out = self.slot("acquire", "--kind", kind, "--who", who, "--timeout", "0.4")
            self.assertEqual(out.returncode, 1, f"{who} granted while a benchmark waits")
            self.assertIn(why, out.stderr)
        text = self.slot("status").stdout
        self.assertIn("backfill: ends ~", text)
        self.assertIn("ETA ~", text)
        self.release(heavy["QC_SLOT_ID"])
        time.sleep(0.3)
        self.assertIsNone(bench.poll(), "benchmark granted beside a backfilled build")
        out = self.slot("acquire", "--kind", "build", "--who", "t1 short", "--timeout", "0.4")
        self.assertIn("nothing backfills", out.stderr, "backfill went on after the slots the bench waited for")
        self.release(short["QC_SLOT_ID"])
        exports = self.granted(bench)
        out = self.slot("acquire", "--kind", "build", "--who", "t1 short", "--timeout", "0.4")
        self.assertEqual(out.returncode, 1, "a build granted during a benchmark")
        self.assertIn("a benchmark holds the machine", out.stderr)
        self.release(exports["QC_SLOT_ID"])

    def test_backfill_stops_after_max_wait(self):
        self.bench_behind_verify(QC_SLOT_BACKFILL_MAX_WAIT="0.3")
        time.sleep(0.5)
        out = self.slot("acquire", "--kind", "build", "--who", "t1 short", "--timeout", "0.4")
        self.assertEqual(out.returncode, 1, "backfill after the bench waited QC_SLOT_BACKFILL_MAX_WAIT")
        self.assertIn("QC_SLOT_BACKFILL_MAX_WAIT", out.stderr)

    def test_backfill_off(self):
        self.bench_behind_verify(QC_SLOT_BACKFILL="0")
        out = self.slot("acquire", "--kind", "build", "--who", "t1 short", "--timeout", "0.4")
        self.assertEqual(out.returncode, 1)
        self.assertIn("QC_SLOT_BACKFILL=0", out.stderr)

    def test_backfill_grace_for_overdue_slots(self):
        """A verify past its usual duration ends 'now': only work within the grace backfills."""
        self.write_history(("verify", "a", 0), ("build", "t1 short", 5), ("build", "t1 short", 5),
                           ("build", "t1 mid", 20), ("build", "t1 mid", 20))
        self.start_server(QC_SLOT_BACKFILL_GRACE="10")
        self.hold("verify", "a")
        self.acquire("bench", "bench")
        wait_until(lambda: self.queued() == ["bench"])
        out = self.slot("acquire", "--kind", "build", "--who", "t1 mid", "--timeout", "0.4")
        self.assertEqual(out.returncode, 1, "work longer than the grace backfilled beside an overdue verify")
        self.assertIn("QC_SLOT_BACKFILL_GRACE", out.stderr)
        self.hold("build", "t1 short")

    def test_short_build_overtakes_long_one_while_bench_waits(self):
        self.bench_behind_verify()
        long = self.acquire("build", "t1 long")
        wait_until(lambda: self.queued() == ["bench", "t1 long"])
        self.hold("build", "t1 short")
        self.assertIsNone(long.poll())
        self.assertEqual(self.queued(), ["bench", "t1 long"])

    def test_overdue_backfill_is_shown_and_blocks_its_label(self):
        self.write_history(("verify", "a", 600), ("build", "t1 quick", 0), ("build", "t1 quick", 0))
        self.start_server(QC_SLOT_BACKFILL_GRACE="0.2")
        self.hold("verify", "a")
        self.acquire("bench", "bench")
        wait_until(lambda: self.queued() == ["bench"])
        self.hold("build", "t1 quick")
        self.assertTrue(wait_until(lambda: "backfill_overdue" in self.holders()["t1 quick"]))
        self.assertIn("overdue backfill delays bench #2", self.slot("status").stdout)
        out = self.slot("acquire", "--kind", "build", "--who", "t1 quick", "--timeout", "0.4")
        self.assertEqual(out.returncode, 1, "a label whose backfill ran over time backfilled again")
        self.assertIn("ran over time", out.stderr)
        with open(os.path.join(self.dir, "qc-slot.log"), encoding="utf-8") as handle:
            self.assertEqual(handle.read().count("backfill over time"), 1, "overdue backfill not logged exactly once")

    def test_backfill_marker_cleared_without_waiting_bench(self):
        heavy, bench = self.bench_behind_verify()
        self.hold("build", "t1 short")
        self.slot("release", "--who", "bench")
        bench.kill()
        self.assertTrue(wait_until(lambda: "backfill_end" not in self.holders()["t1 short"]))

    def test_run_reports_exit_code_and_failed_runs_are_not_timed(self):
        self.start_server()
        self.assertEqual(self.slot("run", "--kind", "build", "--who", "t1 fail", "--", "false").returncode, 1)
        self.assertEqual(self.slot("run", "--kind", "build", "--who", "t1 ok", "--", "true").returncode, 0)
        with open(os.path.join(self.dir, "qc-slot.log"), encoding="utf-8") as handle:
            log = handle.read()
        self.assertIn("who='t1 fail' after 0s: released (exit 1)", log)
        self.assertIn("who='t1 ok' after 0s: released (exit 0)", log)
        module = slot_module()
        self.assertFalse(module.timed_release("released (exit 1)"))
        self.assertTrue(module.timed_release("released (exit 0)"))
        self.assertTrue(module.timed_release("released"), "a release of an older client without exit code")
        self.assertFalse(module.timed_release("client connection closed"))
        self.assertFalse(module.timed_release("client pid 5 ended"))

    def test_version_warning_for_a_newer_broker(self):
        module = slot_module()
        self.assertIn("newer than this client", module.version_warning(module.VERSION + 1, True))
        self.assertIn("old policy", module.version_warning(module.VERSION - 1, False))

    def test_duration_history(self):
        module = slot_module()
        history = module.DurationHistory()
        log = os.path.join(self.base, "log")
        with open(log, "w", encoding="utf-8") as handle:
            handle.write("x release #1 build who='p1 build' after 10s: released\n"
                         "x release #2 build who=\"it's build\" after 30s: holder pid 5 ended\n"
                         "x release #3 build who='p2 build' after 900s: broker stopped\n"
                         "x release #4 test who='p1 test' after 7s: hold time over\n"
                         "x release #6 test who='p2 test' after 9s: released (exit 2)\n"
                         "x grant #5 build jobs=4 who='p1 build' after 3s\n")
        self.assertEqual(history.load_log([log + ".1", log]), 2)
        self.assertEqual(history.estimate("build", "p1 build"), 10)
        self.assertIsNone(history.estimate("build", "p3 build"), "a task needs 3 samples")
        self.assertIsNone(history.estimate("test", "p1 test"), "a hold time over says nothing about the duration")
        self.assertIsNone(history.estimate("test", "p2 test"), "a failed run says nothing about the duration")
        self.assertIsNone(history.pessimistic("build", "p1 build"), "one sample is not enough for a request")
        for seconds in (40, 20, 30):
            history.record("build", "p1 build", seconds)
        self.assertEqual(history.pessimistic("build", "p1 build"), 40, "longest of the last 3")
        history.record("build", "p4 build", 50)
        self.assertEqual(history.estimate("build", "p3 build"), 30, "median of the task 'build'")
        for seconds in (1, 2):
            history.record("build", f"other{seconds}", seconds)
        self.assertEqual(history.estimate("build", "fresh"), 25, "median of the kind (1 2 10 20 30 30 40 50)")

    def test_bench_waits_for_low_load(self):
        self.set_load(9)
        self.start_server()
        bench = self.acquire("bench", "bench")
        wait_until(lambda: self.queued() == ["bench"])
        self.assertIn("load", self.status()["queue"][0]["reason"])
        self.set_load(1.5)
        self.granted(bench)

    def test_load_gate_holds_while_another_slot_runs(self):
        # Cores 8: limit 10. The 4 held jobs explain load 11 (11 - 8 = 3), so the wait limit does not apply.
        self.start_server(QC_SLOT_LOAD_WAIT="0.2")
        self.hold("build", "a", "--jobs", "4")
        self.set_load(11)
        build = self.acquire("build", "b")
        wait_until(lambda: self.queued() == ["b"])
        time.sleep(0.5)
        self.assertIsNone(build.poll(), "a build granted at load 11 beside another slot")
        self.assertIn("QC_SLOT_LOAD_LIMIT", self.status()["queue"][0]["reason"])
        self.set_load(3)
        # Load 3 (the load changed since a's grant, so its jobs no longer count extra): 5 cores, 4 free.
        self.assertEqual(self.granted(build)["QC_BUILD_JOBS"], "4")

    def test_foreign_load_beside_a_small_slot_escapes_one_per_period(self):
        # 2 held jobs x 6 threads cannot explain load 40 on 8 cores: foreign load, the wait limit applies,
        # and after one escape the wait starts again.
        self.start_server(QC_SLOT_LOAD_WAIT="1.0")
        self.hold("build", "a", "--jobs", "2")
        self.set_load(40)
        first = self.acquire("build", "b")
        wait_until(lambda: self.queued() == ["b"])
        second = self.acquire("build", "c")
        wait_until(lambda: len(self.queued()) == 2)
        self.assertIn("foreign load", self.status()["queue"][0]["reason"])
        self.assertEqual(self.granted(first)["QC_BUILD_JOBS"], "2")
        self.assertIn("granted at load 40.0", self.holders()["b"]["note"])
        time.sleep(0.5)
        self.assertIsNone(second.poll(), "two requests escaped the load gate in one wait period")
        self.granted(second)

    def test_held_jobs_weighted_by_threads_explain_the_load(self):
        # 2 held jobs x 6 threads = 12 >= 18 - 8: the broker's own load, no escape after the wait limit.
        self.start_server(QC_SLOT_LOAD_WAIT="0.2")
        self.hold("build", "a", "--jobs", "2")
        self.set_load(18)
        build = self.acquire("build", "b")
        wait_until(lambda: self.queued() == ["b"])
        time.sleep(0.6)
        self.assertIsNone(build.poll(), "escaped beside a slot whose jobs explain the load")
        self.assertNotIn("foreign", self.status()["queue"][0]["reason"])

    def test_light_releases_do_not_restart_the_wait(self):
        self.set_load(12)
        self.start_server(QC_SLOT_LOAD_WAIT="1.0")
        build = self.acquire("build", "b")
        wait_until(lambda: self.queued() == ["b"])
        deadline = time.time() + 4
        while build.poll() is None and time.time() < deadline:
            self.slot("run", "--kind", "light", "--who", "l", "--", "true")
        self.assertIsNotNone(build.poll(), "light slots ending restarted the wait of a build")
        self.granted(build)

    def test_load_gate_never_blocks_an_idle_broker(self):
        self.set_load(12)
        self.start_server(QC_SLOT_LOAD_WAIT="0.6")
        build = self.acquire("build", "b")
        wait_until(lambda: self.queued() == ["b"])
        self.assertIn("nothing else held", self.status()["queue"][0]["reason"])
        exports = self.granted(build)
        self.assertEqual(exports["QC_BUILD_JOBS"], "2", "a loaded machine gets the fewest jobs")
        note = self.holders()["b"]["note"]
        self.assertIn("granted at load 12.0", note)
        self.assertIn("jobs lowered to 2", note)

    def test_wait_limit_counts_from_the_last_release(self):
        self.start_server(QC_SLOT_LOAD_WAIT="0.8")
        first = self.hold("build", "a", "--jobs", "4")
        self.set_load(11)
        build = self.acquire("build", "b")
        time.sleep(1.0)
        self.assertIsNone(build.poll())
        self.release(first["QC_SLOT_ID"])
        time.sleep(0.4)
        self.assertIsNone(build.poll(), "the wait limit counted the time while another slot ran")
        self.granted(build)

    def test_jobs_scale_down_with_load(self):
        self.set_load(5)
        self.start_server()
        self.assertEqual(self.hold("build", "a")["QC_BUILD_JOBS"], "3")
        self.assertIn("jobs lowered to 3 at load 5.0", self.holders()["a"]["note"])
        # Load 5 plus the 3 jobs just granted: 0 cores left, so the heavy minimum of 3.
        self.assertEqual(self.hold("verify", "v")["QC_TEST_JOBS"], "3")
        self.set_load(0)
        self.assertEqual(self.hold("test", "t")["QC_TEST_JOBS"], "2", "free jobs and pending ones still count")

    def test_jobs_just_granted_count_as_load(self):
        self.start_server(QC_SLOT_LOAD_LIMIT="5", QC_SLOT_LOAD_WINDOW="1")
        self.hold("verify", "a", "--jobs", "6")
        build = self.acquire("build", "b")
        wait_until(lambda: self.queued() == ["b"])
        self.assertIn("6 jobs just granted", self.status()["queue"][0]["reason"])
        self.assertEqual(self.granted(build)["QC_BUILD_JOBS"], "2")

    def test_runnable_estimate_inverts_the_load_average(self):
        module = slot_module()
        decay = math.exp(-5 / 60)
        after_one = 2 * decay + 10 * (1 - decay)
        self.assertAlmostEqual(module.runnable_estimate(2, after_one, 4.2), 10)
        after_two = 2 * decay ** 2 + 10 * (1 - decay ** 2)
        self.assertAlmostEqual(module.runnable_estimate(2, after_two, 9.6), 10)
        self.assertEqual(module.runnable_estimate(20, 1, 5), 0)

    def test_monitor_window_and_unchanged_readings(self):
        module = slot_module()
        clock = FakeClock()
        readings = [2.0]
        monitor = module.LoadMonitor(15, source=lambda: (readings[0], 0, 0), clock=clock)
        monitor.sample()
        self.assertEqual(monitor.measure(), (2.0, False), "no sample yet: the 1-min average")
        decay = math.exp(-5 / 60)
        for _ in range(3):
            clock.now += 1
            monitor.sample()
        self.assertEqual(len(monitor.samples), 0, "an unchanged reading added a sample")
        clock.now += 2
        readings[0] = 2 * decay + 10 * (1 - decay)
        monitor.sample()
        self.assertAlmostEqual(monitor.current(), 10)
        # A missed step: two kernel updates (one unchanged) between the readings, n = 4 in both.
        clock.now += 10
        before = readings[0]
        readings[0] = before * decay ** 2 + 4 * (1 - decay ** 2)
        monitor.sample()
        self.assertAlmostEqual(monitor.current(), 7)
        clock.now += 30
        value, from_window = monitor.measure()
        self.assertEqual((value, from_window), (readings[0], False), "old samples left the window")

    def test_monitor_direct_source(self):
        module = slot_module()
        clock = FakeClock()
        values = [3.0]
        monitor = module.LoadMonitor(15, source=lambda: values[0], clock=clock, direct=True)
        monitor.sample()
        self.assertEqual(monitor.measure(), (3.0, True))
        clock.now += 1
        values[0] = 5.0
        monitor.sample()
        self.assertEqual(monitor.measure(), (5.0, True))
        self.assertEqual(monitor.fresh_weight(clock.now - 0.5), 0.0, "a window of one sample, taken after the grant")

    def test_fresh_grant_weight_fades_over_a_window(self):
        module = slot_module()
        clock = FakeClock()
        clock.now = 85.0
        values = [1.0]
        monitor = module.LoadMonitor(15, source=lambda: values[0], clock=clock, direct=True)
        monitor.sample()

        def update(at):
            clock.now = at
            values[0] += 1
            monitor.sample()

        update(90)
        update(95)
        granted = 100.0
        clock.now = 101
        self.assertEqual(monitor.fresh_weight(granted), 1.0, "no load update since the grant")
        update(102)
        self.assertAlmostEqual(monitor.fresh_weight(granted), 2 / 3)
        update(107)
        self.assertAlmostEqual(monitor.fresh_weight(granted), 1 / 3)
        update(112)
        self.assertEqual(monitor.fresh_weight(granted), 0.0)
        self.assertEqual(monitor.fresh_weight(113.0), 1.0)
        clock.now = 129
        self.assertEqual(monitor.fresh_weight(113.0), 0.0, "a whole window without update")

    def test_status_of_an_older_broker(self):
        module = slot_module()
        old = {"version": 1, "pid": 1, "up": 5, "load": 2.0, "cores": 8, "budget": 8, "used": 0, "bench_load": 4,
               "locks": {}, "holders": [], "queue": []}
        printed = io.StringIO()
        with contextlib.redirect_stdout(printed):
            module.print_status(old)
        self.assertIn("load 2 (1 min)", printed.getvalue())
        self.assertIn("stop", module.version_warning(None, True))
        self.assertIsNone(module.version_warning(module.VERSION, True))
        self.start_server()
        self.assertNotIn("WARNING", self.slot("status").stderr)

    def test_pid_death_releases(self):
        self.start_server()
        sleeper = self.sleeper()
        self.hold("verify", "a", "--pid", str(sleeper.pid), "--jobs", "8")
        waiting = self.acquire("build", "b")
        wait_until(lambda: self.queued() == ["b"])
        sleeper.kill()
        sleeper.wait()
        self.granted(waiting)
        self.assertNotIn("a", self.holders())

    def test_killed_run_client_hands_slot_to_command(self):
        self.start_server()
        runner = self.spawn("run", "--kind", "verify", "--who", "a", "--jobs", "8", "--", "sleep", "30")
        wait_until(lambda: "a" in self.holders())
        waiting = self.acquire("build", "b")
        wait_until(lambda: self.queued() == ["b"])
        runner.kill()
        runner.wait()
        holder = wait_until(lambda: self.holders().get("a", {}).get("lease") == "pid" and self.holders()["a"])
        self.assertTrue(holder, "the slot ended with the client although its command still runs")
        time.sleep(0.3)
        self.assertIsNone(waiting.poll(), "slot freed while the orphaned command still runs")
        os.kill(holder["pid"], 9)
        self.granted(waiting)

    def test_run_client_releases_when_command_ends(self):
        self.start_server()
        runner = self.spawn("run", "--kind", "verify", "--who", "a", "--jobs", "8", "--", "sleep", "0.5")
        wait_until(lambda: "a" in self.holders())
        waiting = self.acquire("build", "b")
        self.granted(waiting)
        self.assertEqual(runner.wait(timeout=5), 0)

    def test_stale_bench_lock_is_replaced(self):
        self.start_server()
        dead = subprocess.Popen(["true"])
        dead.wait()
        self.make_lock("bench.lock", dead.pid)
        exports = self.hold("bench", "bench")
        with open(os.path.join(self.dir, "bench.lock", "owner"), encoding="utf-8") as handle:
            self.assertIn("tool=qc-slot.py", handle.read())
        self.release(exports["QC_SLOT_ID"])
        self.assertFalse(os.path.exists(os.path.join(self.dir, "bench.lock")))

    def test_wait_inside_own_bench_slot(self):
        self.start_server()
        bench = self.hold("bench", "bench")
        self.assertEqual(self.slot("wait", "--timeout", "0.3").returncode, 1, "wait passed a running benchmark")
        inside = {**self.env, "QC_SLOT_ID": bench["QC_SLOT_ID"]}
        self.assertEqual(self.slot("wait", "--timeout", "2", env=inside).returncode, 0, "a bench waits for itself")
        child = {**self.env, "QC_SLOT_ID": "999", "QC_BENCH_LOCK_PID": bench["QC_BENCH_LOCK_PID"]}
        self.assertEqual(self.slot("wait", "--timeout", "2", env=child).returncode, 0)

    def test_bench_granted_under_load_says_so(self):
        self.set_load(9)
        self.start_server(QC_SLOT_LOAD_WAIT="0.3")
        out = self.slot("acquire", "--kind", "bench", "--who", "bench", "--hold", "60")
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertIn("# note: granted at load 9.0", out.stdout)
        self.assertIn("granted at load", self.holders()["bench"]["note"])

    def test_broker_restart_while_waiting_requeues(self):
        self.start_server()
        self.hold("verify", "a", "--jobs", "8")
        waiting = self.acquire("build", "b")
        wait_until(lambda: self.queued() == ["b"])
        old = self.status()["pid"]
        self.assertEqual(self.slot("stop", "--force").returncode, 0)
        self.server.wait(timeout=5)
        out, err = waiting.communicate(timeout=10)
        self.assertEqual(waiting.returncode, 0, err)
        self.assertIn("export QC_SLOT_ID=", out)
        self.assertNotIn("fallback", out, err)
        self.assertNotEqual(self.status()["pid"], old)

    def test_long_socket_path_falls_back_at_once(self):
        long_dir = os.path.join(self.base, "x" * 110)
        os.makedirs(long_dir)
        env = {**self.env, "QC_SLOT_DIR": long_dir}
        started = time.time()
        out = self.slot("run", "--kind", "build", "--who", "r", "--", "true", env=env)
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertIn("socket path longer", out.stderr)
        self.assertLess(time.time() - started, 3)
        self.assertEqual(self.slot("run", "--no-fallback", "--kind", "build", "--", "true", env=env).returncode, 211)

    def test_run_releases_and_passes_exit_code(self):
        self.start_server()
        out = self.slot("run", "--kind", "build", "--who", "r", "--", "sh", "-c", "exit 7")
        self.assertEqual(out.returncode, 7)
        self.assertEqual(self.status()["holders"], [])

    def test_timeout(self):
        self.start_server()
        self.hold("verify", "a", "--jobs", "8")
        out = self.slot("acquire", "--kind", "build", "--who", "b", "--timeout", "0.3")
        self.assertEqual(out.returncode, 1)
        self.assertEqual(self.queued(), [])

    def test_foreign_lock_files(self):
        self.start_server()
        sleeper = self.sleeper()
        quiet = self.make_lock("quiet.lock", sleeper.pid)
        build = self.acquire("build", "b")
        wait_until(lambda: self.queued() == ["b"])
        self.assertIn("quiet.lock", self.status()["queue"][0]["reason"])
        self.assertEqual(self.hold("light", "l")["QC_SLOT_JOBS"], "1")
        # A lock of a process that ended is stale: ignored, and left for the shell scripts.
        sleeper.kill()
        sleeper.wait()
        self.granted(build)
        self.assertTrue(os.path.isdir(quiet))
        shutil.rmtree(quiet)
        other = self.sleeper()
        self.make_lock("bench.lock", other.pid)
        bench = self.acquire("bench", "bench")
        light = self.acquire("light", "l2")
        wait_until(lambda: len(self.queued()) == 2)
        self.assertIn("foreign bench.lock", self.status()["queue"][0]["reason"])
        self.assertIsNone(light.poll())
        shutil.rmtree(os.path.join(self.dir, "bench.lock"))
        self.granted(light)

    def test_wait_gate(self):
        self.start_server()
        heavy = self.hold("verify", "a", "--jobs", "8")
        self.assertEqual(self.slot("wait", "--timeout", "2").returncode, 0, "wait blocked by a verify slot")
        bench = self.acquire("bench", "bench")
        wait_until(lambda: self.queued() == ["bench"])
        self.assertEqual(self.slot("wait", "--timeout", "0.3").returncode, 1, "wait passed a waiting benchmark")
        inside = {**self.env, "QC_SLOT_ID": heavy["QC_SLOT_ID"]}
        self.assertEqual(self.slot("wait", "--timeout", "2", env=inside).returncode, 0, "a slot holder waits for a bench that waits for it")
        self.release(heavy["QC_SLOT_ID"])
        self.granted(bench)

    def test_fallback_without_broker(self):
        env = {**self.env, "QC_SLOT": "0"}
        out = self.slot("run", "--kind", "bench", "--who", "f", "--", "sh", "-c",
                        'test -d "$QC_SLOT_DIR/bench.lock" && echo $QC_SLOT_ID', env=env)
        self.assertEqual((out.returncode, out.stdout.strip()), (0, "fallback"), out.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.dir, "bench.lock")))
        self.assertEqual(self.slot("run", "--no-fallback", "--kind", "build", "--", "true", env=env).returncode, 211)
        sleeper = self.sleeper()
        self.make_lock("quiet.lock", sleeper.pid)
        self.assertEqual(self.slot("wait", "--timeout", "0.3", env=env).returncode, 1)
        self.assertFalse(os.path.exists(os.path.join(self.dir, "qc-slot.sock")), "QC_SLOT=0 started a broker")

    def test_autostart_and_single_instance(self):
        out = self.slot("run", "--kind", "build", "--who", "r", "--", "true")
        self.assertEqual(out.returncode, 0, out.stderr)
        pid = self.status()["pid"]
        second = self.slot("serve")
        self.assertIn("already runs", second.stderr)
        self.assertEqual(self.status()["pid"], pid)

    def test_wait_quiet_uses_broker(self):
        env = {**self.env, "QC_QUIET_PAUSE_MAX": "1", "QC_QUIET_POLL": "1"}
        run = lambda: subprocess.run(["sh", WAIT_QUIET], env=env, capture_output=True, text=True, timeout=20)
        self.assertEqual(run().returncode, 0)
        self.assertTrue(os.path.exists(os.path.join(self.dir, "qc-slot.sock")), "wait-quiet.sh did not use the broker")
        sleeper = self.sleeper()
        self.make_lock("bench.lock", sleeper.pid)
        self.assertEqual(run().returncode, 1)
        own = {**env, "QC_BENCH_LOCK_PID": str(sleeper.pid)}
        self.assertEqual(subprocess.run(["sh", WAIT_QUIET], env=own, timeout=20).returncode, 0)


if __name__ == "__main__":
    unittest.main()
