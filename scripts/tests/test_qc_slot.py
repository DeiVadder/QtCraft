#!/usr/bin/env python3
"""Tests of the slot broker scripts/qc-slot.py (decision E-781).

Every test runs its own broker in a temporary slot directory (QC_SLOT_DIR and HOME point there), with
a load read from a file, 8 cores and a tick of 50 ms, so nothing touches ~/.cache/qtcraft and the
whole file runs in a few seconds. Run: python3 scripts/tests/test_qc_slot.py (verify.sh runs it in
the step check-slot-broker).
"""

import json
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

    def test_bench_waits_for_low_load(self):
        self.set_load(9)
        self.start_server()
        bench = self.acquire("bench", "bench")
        wait_until(lambda: self.queued() == ["bench"])
        self.assertIn("load", self.status()["queue"][0]["reason"])
        self.set_load(1.5)
        self.granted(bench)

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
