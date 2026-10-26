#!/usr/bin/env python3
"""Slot broker for builds, verify runs and benchmarks of several agents on one machine (decision E-781).

A small daemon hands out slots per kind of work and blocks the caller until its slot is granted, so
nobody polls lock files. The first client starts the daemon when none runs (one instance per slot
directory; Unix socket, log and pid file in ~/.cache/qtcraft, QC_SLOT_DIR overrides; it ends by
itself after QC_SLOT_IDLE seconds without work, default 900).

Usage:
  scripts/qc-slot.py run --kind <kind> --who <label> [--jobs n] [--min-jobs n] [--timeout s] -- <command> ...
      Waits for the slot, runs the command with QC_BUILD_JOBS, QC_TEST_JOBS and QC_SLOT_JOBS set to
      the granted job count (an already set QC_BUILD_JOBS/QC_TEST_JOBS only lowers it) and releases
      the slot when the command ends; the exit code is the command's. The open connection is the
      lease (a killed client hands the slot to its command, which keeps it until it ends; a broker
      that ends meanwhile gets the slot registered again by the next one). Inside a slot that covers
      the kind (bench covers all, tsan/asan/verify cover build/test/heavy, build/test cover each
      other; QC_SLOT_ID and QC_SLOT_KIND) it runs the command directly. This is the entry point for
      agents. Example (as a background command; the agent is woken when it returns):
        scripts/qc-slot.py run --kind build --who "t1 build" -- cmake --build --preset debug
  scripts/qc-slot.py acquire --kind <kind> --who <label> [--pid p | --hold s] [--jobs n] [--min-jobs n] [--timeout s]
      Waits for the slot and prints it as shell exports (QC_SLOT_ID, QC_SLOT_JOBS, QC_BUILD_JOBS,
      QC_TEST_JOBS, for a bench QC_BENCH_LOCK_PID). The slot stays held until `release --id`, until
      process --pid ends, or after --hold seconds (default 7200; a bench slot needs --pid or a hold of
      at most 1800 s). For scripts that cannot wrap their work; agents use run.
  scripts/qc-slot.py release --id <id> | --who <label>
  scripts/qc-slot.py wait [--who <label>] [--timeout s]
      Returns once no benchmark holds or awaits the machine and no timing phase (quiet.lock) runs;
      takes no slot (scripts/wait-quiet.sh). Inside a slot (QC_SLOT_ID set) a waiting benchmark
      does not count: it waits for that slot.
  scripts/qc-slot.py status [--json]      load, holders (backfill end), queue with waiting times, reasons, ETA
  scripts/qc-slot.py serve                runs the daemon in the foreground
  scripts/qc-slot.py stop [--force]       ends the daemon (refused while slots are held or awaited)

Kinds: bench (exclusive: no build/test/verify slot held, 1-min load below QC_SLOT_BENCH_LOAD,
default 4; while a bench waits only light and backfill slots are granted, running ones finish;
backfill (E-847): a build or test slot whose longest of the last 3 successful runs of the same
label (at least 2, from the log) ends before the slots granted before the bench are expected to
end (median of their label, task or kind), plus QC_SLOT_BACKFILL_GRACE (default 60 s); none
without that history, once those slots ended, after the bench waited QC_SLOT_BACKFILL_MAX_WAIT
seconds (default 1800), or for a label whose last backfill ran over time (never killed; shown in
status) until it has a fresh duration; QC_SLOT_BACKFILL=0 turns it off),
tsan/asan/verify (heavy; job wish max(3, cores/2)), build (wish 4), test (wish 3), light (1 job,
outside the budget; only waits for a benchmark). Heavy, build and test share a budget of
QC_SLOT_BUDGET jobs (default the number of cores): a grant gets min(wish, free jobs, cores - load)
and never fewer than the request's minimum (E-892): --min-jobs, else 4 for tsan/asan/verify (with
--jobs at least half the wish) and 1 for build/test/light, at most the wish (bench stays exclusive);
the load check asks at most half the cores free. A request whose
minimum the budget or the load does not allow waits (reason "needs ≥4 jobs, load ... allows N");
against foreign load only QC_SLOT_LOAD_WAIT seconds, then it gets its minimum. While a long request
(minimum 4 or more) waits, a later short one starts with its minimum (1 job) if the short slots
still leave the long one its minimum of the budget; after the long one waited QC_SLOT_LOAD_WAIT
seconds for the load, later requests wait until it started. A build of a sanitizer or release
tree passes --min-jobs 4. They also wait while the load
is at or above QC_SLOT_LOAD_LIMIT (default 1.25 x cores); the load is the mean number of runnable
threads of the last QC_SLOT_LOAD_WINDOW seconds (default 15, derived from successive 1-min load
averages sampled every second, so it follows a starting or ending run within seconds) plus the jobs
of fresh grants until a full window of samples passed after their first load update (not yet fully
visible in it). While held
build/test/heavy slots explain the load (their jobs x QC_SLOT_THREADS_PER_JOB, default 6, at least
load - cores) the gate holds without
limit (those slots end and the load drops); otherwise (none held, or the load comes mostly from
processes outside the broker) it holds a request at most QC_SLOT_LOAD_WAIT seconds from its arrival,
the last release of such a slot or the last such escape (default 600; one request escapes per
period; the grant carries a note). Order: light, bench, then build, test and heavy slots first come
first served in one queue; a request that waits for jobs is not overtaken by any later one, except
by short ones with their minimum behind a long one (above).
Lock files of scripts without the broker keep working: no slot is granted while a live foreign
~/.cache/qtcraft/bench.lock exists (light and wait included) or a live quiet.lock of a timing phase
(except light); a granted bench creates bench.lock itself (owner "bench-slot.sh pid=<holder> ...",
the holder's children see QC_BENCH_LOCK_PID). Holders whose process ended are released within
QC_SLOT_TICK seconds (default 2), also after a sleep of the machine.
QC_SLOT=0, or a daemon that cannot start, falls back to the lock files alone: wait while a foreign
lock is held (checked every QC_SLOT_POLL s, default 10), default job counts. With --no-fallback the
client exits 211 instead, and the shell scripts use their own lock handling.
Exit codes: the command's (run), 0 granted, 1 timeout, 2 usage, 211 broker unavailable (--no-fallback).
"""

import argparse
import fcntl
import json
import math
import os
import re
import select
import signal
import socket
import statistics
import subprocess
import sys
import threading
import time
from collections import deque

HEAVY = ("tsan", "asan", "verify")
COUNTED = HEAVY + ("build", "test")
KINDS = ("bench",) + COUNTED + ("light",)
# Light work and gates first, then benchmarks; build, test and heavy slots share one first-come queue.
PRIORITY = {"light": 0, "gate": 0, "bench": 1, "build": 2, "test": 2, "tsan": 2, "asan": 2, "verify": 2}
# Fewest jobs a kind starts with (E-892): long runs keep their grant for hours, so they wait for 4 jobs
# rather than starting starved; short ones run with 1. --min-jobs overrides it per request.
MIN_JOBS = {"build": 1, "test": 1, "light": 1, "tsan": 4, "asan": 4, "verify": 4}
# Threads a granted job of a build runs (one compiler process, a little linking), for the check
# whether held slots explain the load in Broker.room_refusal; other kinds use QC_SLOT_THREADS_PER_JOB.
BUILD_THREADS_PER_JOB = 2
# A request whose minimum reaches this is long: while it waits, later short ones get at most their minimum.
LONG_MIN = 4
# Outside the range of signal deaths (128 + n) and unused by verify.sh and bench-ab.sh, so the shell
# wrappers never mistake an exit code of the script they ran under the broker for "no broker".
EXIT_UNAVAILABLE = 211
SOCKET_PATH_MAX = 103
BENCH_HOLD_MAX = 1800
# Seconds between two kernel updates of the load average.
LOADAVG_PERIOD = 5.0
# Seconds between two load samples; must stay well below LOADAVG_PERIOD.
LOAD_SAMPLE_SECONDS = 1.0
VERSION = 4
# Kinds that may backfill while a benchmark waits (E-847); light never waits for a waiting bench.
BACKFILL_KINDS = ("build", "test")
# A release line of the log: kind, label (repr, either quote), seconds held, reason.
RELEASE_LINE = re.compile(r"release #\d+ (\w+) who=(['\"])(.*)\2 after (\d+)s: (.*)$")
# Releases that say nothing about how long the work takes (cut short, or the client vanished).
UNTIMED_RELEASES = ("broker stopped", "hold time over", "client gone", "client connection closed", "client pid")
# Exit code of the command in a release reason ("released (exit 2)"); runs that failed are not timed.
EXIT_IN_REASON = re.compile(r"\(exit (-?\d+)\)")
# Bytes read from the end of each log at start (the log rotates at about 1 MB).
HISTORY_LOG_TAIL = 2_000_000


def slot_dir():
    return os.environ.get("QC_SLOT_DIR") or os.path.join(os.path.expanduser("~"), ".cache", "qtcraft")


def number(name, default):
    value = os.environ.get(name, "")
    try:
        return float(value) if value else float(default)
    except ValueError:
        print(f"qc-slot: {name} must be a number, not '{value}'; using {default}", file=sys.stderr)
        return float(default)


def core_count():
    return int(number("QC_SLOT_CORES", os.cpu_count() or 4))


def default_wish(kind, cores):
    """Jobs a kind asks for when the caller names none."""
    if kind in HEAVY:
        return max(3, cores // 2)
    return {"build": 4, "test": 3, "light": 1, "bench": cores}.get(kind, 0)


def pid_alive(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def fake_load():
    """The load read from the file QC_SLOT_FAKE_LOAD (tests), or None without it."""
    fake = os.environ.get("QC_SLOT_FAKE_LOAD")
    if not fake:
        return None
    try:
        with open(fake, encoding="utf-8") as handle:
            return float(handle.read().strip() or 0)
    except (OSError, ValueError):
        return 0.0


def machine_load():
    """The 1-min load average (or the fake load of the tests)."""
    fake = fake_load()
    return fake if fake is not None else os.getloadavg()[0]


def runnable_estimate(previous, current, elapsed):
    """Runnable threads the kernel counted, from two 1-min load averages seen `elapsed` seconds apart.

    macOS and Linux update the average every 5 s as L = L * e + n * (1 - e) with e = exp(-5/60);
    solving for n gives the number of runnable threads now, without the minute of lag of the average.
    """
    steps = max(1, round(elapsed / LOADAVG_PERIOD))
    decay = math.exp(-steps * LOADAVG_PERIOD / 60)
    return max(0.0, (current - previous * decay) / (1 - decay))


class LoadMonitor:
    """Short-term load: mean of the runnable threads derived from the 1-min average over a window.

    source() gives the 1-min load average, clock() a monotonic time (both injectable for tests).
    The broker samples every LOAD_SAMPLE_SECONDS on its own timer, well below the 5 s between two
    kernel updates, so every update is seen (a reading that did not change adds nothing; a later
    change spanning several updates is inverted over all of them). Without samples in the window
    (just started, or a steady load) the 1-min average itself is the value. With direct=True
    (QC_SLOT_FAKE_LOAD of the tests) the source is the short-term load itself.
    """

    def __init__(self, window, source=os.getloadavg, clock=time.monotonic, direct=False):
        self.window = window
        self.source = source
        self.clock = clock
        self.direct = direct
        self.lock = threading.Lock()
        self.last = None
        self.samples = deque()

    def sample(self):
        now = self.clock()
        value = float(self.source()) if self.direct else self.source()[0]
        with self.lock:
            if self.last is not None and value == self.last[1]:
                return
            if self.last is not None:
                estimate = value if self.direct else runnable_estimate(self.last[1], value, now - self.last[0])
                self.samples.append((now, estimate))
            self.last = (now, value)

    def measure(self):
        """(load, whether it is the window mean; False: the 1-min average)."""
        if self.direct:
            return float(self.source()), True
        with self.lock:
            self.prune()
            if self.samples:
                return sum(n for _, n in self.samples) / len(self.samples), True
        return self.source()[0], False

    def current(self):
        return self.measure()[0]

    def prune(self):
        """Drops samples older than the window; runs under self.lock."""
        limit = self.clock() - self.window
        while self.samples and self.samples[0][0] < limit:
            self.samples.popleft()

    def fresh_weight(self, granted):
        """Share of a grant's jobs the measured load does not show yet (1 down to 0).

        Until the first load update after the grant the load cannot show it (1; 0 once a whole window
        passed without one). From then on: 1 - samples since that update / samples in the window, so
        the jobs count until the window is filled with samples taken after the grant.
        """
        with self.lock:
            self.prune()
            after = [t for t, _ in self.samples if t > granted]
            if not after:
                return 1.0 if self.clock() - granted < self.window else 0.0
            return 1.0 - len(after) / len(self.samples)


def timed_release(why):
    """Whether a release reason gives a usable duration: not cut short, and no failed exit code."""
    if why.startswith(UNTIMED_RELEASES):
        return False
    code = EXIT_IN_REASON.search(why)
    return code is None or code.group(1) == "0"


def tail_lines(path, size):
    """The complete lines of the last `size` bytes of a file ([] when it cannot be read)."""
    try:
        with open(path, "rb") as handle:
            handle.seek(0, os.SEEK_END)
            start = max(0, handle.tell() - size)
            handle.seek(start)
            data = handle.read()
    except OSError:
        return []
    lines = data.decode("utf-8", errors="replace").splitlines()
    return lines[1:] if start > 0 else lines


def clock_text(when):
    return time.strftime("%H:%M", time.localtime(when))


class DurationHistory:
    """How long slots of a kind and label took, learned from the release lines of the broker log.

    estimate() (for running slots) gives the median of the recent durations of the same label (one
    sample is enough), else of the same task (the label from its second word on, i.e. without the
    leading package word: "76.0b build" and "t1 build" share "build"; at least 3), else of the kind
    (at least 5); None when nothing is known. pessimistic() (for a request that wants to backfill)
    gives the longest of the label's last 3 durations and needs at least 2 of them.
    """

    KEEP = 15
    KEEP_KIND = 50
    NEEDED = {"who": 1, "task": 3, "kind": 5}

    def __init__(self):
        self.samples = {}

    @staticmethod
    def keys(kind, who):
        words = who.split(" ", 1)
        task = words[1] if len(words) > 1 else who
        return [("who", kind, who), ("task", kind, task), ("kind", kind, "")]

    def record(self, kind, who, seconds):
        for key in self.keys(kind, who):
            keep = self.KEEP_KIND if key[0] == "kind" else self.KEEP
            self.samples.setdefault(key, deque(maxlen=keep)).append(float(seconds))

    def estimate(self, kind, who):
        for key in self.keys(kind, who):
            values = self.samples.get(key)
            if values and len(values) >= self.NEEDED[key[0]]:
                return statistics.median(values)
        return None

    def pessimistic(self, kind, who):
        values = self.samples.get(("who", kind, who))
        if not values or len(values) < 2:
            return None
        return max(list(values)[-3:])

    def load_log(self, paths):
        """Reads the release lines of the last HISTORY_LOG_TAIL bytes of the given logs, oldest first;
        returns the number of samples."""
        count = 0
        for path in paths:
            for line in tail_lines(path, HISTORY_LOG_TAIL):
                match = RELEASE_LINE.search(line)
                if match and timed_release(match.group(5)):
                    self.record(match.group(1), match.group(3), int(match.group(4)))
                    count += 1
        return count


def lock_holder(path, ignore):
    """Owner line of a live lock directory of another process, or None when free or stale.

    A lock whose owner pid no longer runs is stale; it is left for the shell scripts to remove. A
    quiet.lock without owner file counts as held for a minute (being taken), a bench.lock always.
    """
    if not os.path.isdir(path):
        return None
    try:
        with open(os.path.join(path, "owner"), encoding="utf-8") as handle:
            owner = handle.read().strip()
    except OSError:
        owner = ""
    if not owner:
        try:
            age = time.time() - os.stat(path).st_mtime
        except OSError:
            return None
        if path.endswith("quiet.lock") and age > 60:
            return None
        return "no owner file"
    match = re.search(r"\bpid=(\d+)", owner)
    if match:
        pid = int(match.group(1))
        if pid in ignore or not pid_alive(pid):
            return None
    return owner


def remove_stale_lock(path, log):
    """Removes a lock directory whose owner process ended, if it is still that stale lock."""
    try:
        with open(os.path.join(path, "owner"), encoding="utf-8") as handle:
            owner = handle.read().strip()
    except OSError:
        owner = ""
    if lock_holder(path, set()) is not None:
        return
    try:
        if owner:
            os.remove(os.path.join(path, "owner"))
        os.rmdir(path)
        log(f"removed stale {os.path.basename(path)} ({owner or 'no owner file'})")
    except OSError:
        pass


def lock_paths(directory):
    return os.path.join(directory, "bench.lock"), os.path.join(directory, "quiet.lock")


def foreign_lock(directory, ignore, with_quiet):
    """Text naming the foreign lock that blocks a grant, or None."""
    bench, quiet = lock_paths(directory)
    owner = lock_holder(bench, ignore)
    if owner:
        return f"foreign bench.lock ({owner})"
    owner = lock_holder(quiet, ignore) if with_quiet else None
    if owner:
        return f"quiet.lock of a timing phase ({owner})"
    return None


def send_json(sock, data):
    sock.sendall((json.dumps(data) + "\n").encode())


class LineReader:
    """Reads newline-separated JSON objects from a socket."""

    def __init__(self, sock):
        self.sock = sock
        self.buffer = b""

    def read(self):
        while b"\n" not in self.buffer:
            try:
                chunk = self.sock.recv(4096)
            except OSError:
                return None
            if not chunk:
                return None
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        try:
            return json.loads(line)
        except ValueError:
            return None


class Entry:
    """One request: waiting in the queue, then holding its slot."""

    def __init__(self, ident, request, cores):
        self.id = ident
        self.kind = request["kind"]
        self.who = re.sub(r"\s+", " ", str(request.get("who") or "?"))[:120]
        self.client_pid = int(request.get("client_pid") or 0)
        self.pid = int(request["pid"]) if request.get("pid") else None
        self.lease = request.get("lease", "conn")
        self.hold = float(request.get("hold") or 7200)
        self.cwd = str(request.get("cwd") or "?")
        self.ignore = {int(p) for p in request.get("ignore", []) if str(p).isdigit()}
        # Asked from inside a granted slot: a waiting benchmark waits for that slot, so it must not
        # hold this request back (no deadlock).
        self.inside_slot = bool(request.get("inside_slot"))
        slot_id = str(request.get("slot_id") or "")
        self.slot_id = int(slot_id) if slot_id.isdigit() else None
        self.child_pid = None
        self.note = ""
        self.escaped = False
        wish = request.get("jobs") or default_wish(self.kind, cores)
        minimum = request.get("min_jobs")
        self.min_explicit = int(minimum) if str(minimum or "").isdigit() and int(minimum) > 0 else None
        # A minimum above the wish raises the wish: it would never be granted otherwise.
        self.wish = max(1, int(wish), self.min_explicit or 0)
        self.jobs_explicit = bool(request.get("jobs_explicit"))
        # What the request waits for in the queue: "budget", "load" (jobs below its minimum) or None.
        self.waits_for = None
        timeout = float(request.get("timeout") or 0)
        self.since = time.time()
        self.since_mono = time.monotonic()
        self.granted_mono = None
        self.deadline = self.since + timeout if timeout > 0 else None
        self.granted_at = None
        self.expires = None
        self.jobs = 0
        self.reason = "queued"
        self.result = None
        self.lock_owner = None
        # Expected end (wall time) of a slot granted as backfill while a benchmark waited (E-847).
        self.backfill_end = None
        # The benchmark a backfilled slot was granted beside, and whether it ran over time (E-847).
        self.backfill_bench = None
        self.overdue = False
        # Expected grant time (wall) of a waiting request, when the broker can tell.
        self.eta = None
        self.done = threading.Event()

    def min_jobs(self):
        """Fewest jobs the request starts with (E-892): --min-jobs, else the kind's minimum (a heavy run
        sized with --jobs at least half of it), never above the wish (an own lower job count wins)."""
        if self.min_explicit:
            return self.min_explicit
        minimum = MIN_JOBS.get(self.kind, 1)
        if self.kind in HEAVY and self.jobs_explicit:
            minimum = max(minimum, self.wish // 2)
        return max(1, min(minimum, self.wish))

    def is_long(self):
        return self.kind in COUNTED and self.min_jobs() >= LONG_MIN

    def holder_pid(self):
        if self.lease == "pid" and self.pid:
            return self.pid
        if self.lease == "conn" and self.client_pid:
            return self.client_pid
        return os.getpid()

    def view(self, now):
        data = {"id": self.id, "kind": self.kind, "who": self.who, "pid": self.holder_pid(), "lease": self.lease,
                "min": self.min_jobs()}
        if self.granted_at is None:
            data.update(waited=round(now - self.since, 1), reason=self.reason, wish=self.wish)
            if self.eta:
                data["eta"] = round(self.eta)
        else:
            data.update(since=self.granted_at, held=round(now - self.granted_at, 1), jobs=self.jobs)
            if self.expires:
                data["expires_in"] = round(self.expires - now)
            if self.note:
                data["note"] = self.note
            if self.backfill_end:
                data["backfill_end"] = round(self.backfill_end)
                if self.overdue:
                    data["backfill_overdue"] = self.backfill_bench
        return data


class Broker:
    """Queue, holders and the grant policy; every method runs under self.cond."""

    def __init__(self, directory, log):
        self.dir = directory
        self.log = log
        self.cores = core_count()
        self.budget = max(1, int(number("QC_SLOT_BUDGET", self.cores)))
        self.bench_load = number("QC_SLOT_BENCH_LOAD", 4)
        self.load_wait = number("QC_SLOT_LOAD_WAIT", 600)
        self.load_limit = number("QC_SLOT_LOAD_LIMIT", 1.25 * self.cores)
        self.load_window = number("QC_SLOT_LOAD_WINDOW", 15)
        # Threads one granted job runs on average (ctest jobs x test threads, compiler processes).
        self.threads_per_job = number("QC_SLOT_THREADS_PER_JOB", 6)
        if fake_load() is not None:
            self.monitor = LoadMonitor(self.load_window, source=fake_load, direct=True)
        else:
            self.monitor = LoadMonitor(self.load_window)
        # QC_SLOT_LOAD_WAIT counts from here (monotonic): the last release of a build/test/heavy slot
        # or the last grant that escaped the load gate, so at most one request escapes per period.
        self.load_wait_since = time.monotonic()
        self.tick_seconds = number("QC_SLOT_TICK", 2)
        # Backfill while a benchmark waits (E-847).
        self.backfill = os.environ.get("QC_SLOT_BACKFILL", "1") != "0"
        self.backfill_grace = number("QC_SLOT_BACKFILL_GRACE", 60)
        self.backfill_max_wait = number("QC_SLOT_BACKFILL_MAX_WAIT", 1800)
        self.history = DurationHistory()
        # Labels (kind, who) whose last backfill ran over time: no backfill until a fresh duration.
        self.backfill_blocked = set()
        log_path = os.path.join(directory, "qc-slot.log")
        self.history_loaded = self.history.load_log([log_path + ".1", log_path])
        self.idle_seconds = number("QC_SLOT_IDLE", 900)
        self.cond = threading.Condition()
        self.queue = []
        self.holders = {}
        self.next_id = 1
        self.connections = 0
        self.last_activity = time.time()
        self.started = time.time()
        self.stopping = False

    def submit(self, request):
        with self.cond:
            entry = Entry(self.next_id, request, self.cores)
            self.next_id += 1
            self.queue.append(entry)
            self.log(f"request #{entry.id} {entry.kind} wish={entry.wish} who={entry.who!r} pid={entry.holder_pid()}")
            self.schedule()
            return entry

    def adopt(self, request):
        with self.cond:
            wanted = request.get("id")
            queued = {e.id for e in self.queue}
            free = isinstance(wanted, int) and wanted > 0 and wanted not in self.holders and wanted not in queued
            ident = wanted if free else self.next_id
            # New ids always lie above every adopted one.
            self.next_id = max(self.next_id, ident + 1)
            entry = Entry(ident, request, self.cores)
            if request.get("since"):
                entry.since = float(request["since"])
                entry.since_mono = time.monotonic() - max(0.0, time.time() - entry.since)
            entry.granted_mono = entry.since_mono
            entry.jobs = int(request.get("jobs") or 0)
            entry.wish = max(entry.wish, entry.jobs)
            entry.granted_at = entry.since
            entry.result = "granted"
            entry.reason = ""
            entry.child_pid = int(request["child_pid"]) if str(request.get("child_pid", "")).isdigit() else None
            if entry.kind == "bench":
                self.adopt_bench_lock(entry)
            if not free and wanted:
                entry.note = f"adopted as #{ident}; its old id #{wanted} is taken here"
            self.holders[entry.id] = entry
            self.log(f"adopt #{entry.id} {entry.kind} jobs={entry.jobs} who={entry.who!r}: slot of an ended broker")
            self.schedule()
            return entry

    def adopt_bench_lock(self, entry):
        path = lock_paths(self.dir)[0]
        try:
            with open(os.path.join(path, "owner"), encoding="utf-8") as handle:
                owner = handle.read().strip()
        except OSError:
            owner = ""
        if owner and f"pid={entry.holder_pid()} " in owner + " ":
            entry.lock_owner = owner
        elif not os.path.isdir(path):
            self.take_bench_lock(entry)

    def cancel(self, entry, why):
        with self.cond:
            if entry in self.queue:
                self.queue.remove(entry)
                entry.result = why
                entry.done.set()
                self.log(f"cancel #{entry.id} {entry.kind} who={entry.who!r}: {why}")
            elif entry.id in self.holders:
                self.release(entry.id, why)
            self.schedule()

    def release(self, ident, why, ended=False, exit_code=None):
        """Releases a slot; with the command's exit code (op release of a newer client) only a run that
        succeeded adds its duration to the history."""
        if exit_code is not None:
            why = f"{why} (exit {exit_code})"
        with self.cond:
            entry = self.holders.pop(ident, None)
            if entry is None:
                return None
            self.drop_bench_lock(entry, ended)
            if entry.kind in COUNTED and not any(h.kind in COUNTED for h in self.holders.values()):
                self.load_wait_since = time.monotonic()
            if entry.granted_mono is not None:
                held = time.monotonic() - entry.granted_mono
            else:
                held = time.time() - (entry.granted_at or entry.since)
            if timed_release(why):
                self.history.record(entry.kind, entry.who, round(held))
                self.backfill_blocked.discard((entry.kind, entry.who))
            self.log(f"release #{entry.id} {entry.kind} who={entry.who!r} after {held:.0f}s: {why}")
            self.last_activity = time.time()
            self.schedule()
            return entry

    def release_who(self, who, why):
        with self.cond:
            idents = [h.id for h in self.holders.values() if h.who == who]
            return [self.release(i, why).id for i in idents]

    def used_jobs(self):
        return sum(h.jobs for h in self.holders.values() if h.kind in COUNTED)

    def pending_jobs(self):
        """Jobs of fresh grants the measured load does not show yet, weighted by the share of the
        window's samples taken before the grant's first load update (LoadMonitor.fresh_weight)."""
        total = sum(h.jobs * self.monitor.fresh_weight(h.granted_mono) for h in self.holders.values()
                    if h.kind in COUNTED and h.granted_mono is not None)
        return round(total, 1)

    def gate_load(self):
        """The load the gate of build/test/heavy slots compares: short-term load plus pending jobs."""
        return self.monitor.current() + self.pending_jobs()

    def schedule(self):
        with self.cond:
            if self.stopping:
                return
            load = machine_load()
            ordered = sorted(self.queue, key=lambda e: (PRIORITY[e.kind], e.id))
            bench_waiting = next((e for e in ordered if e.kind == "bench"), None)
            if bench_waiting is None:
                # Backfill markers only matter while a benchmark waits for the slot.
                for holder in self.holders.values():
                    holder.backfill_end = None
                    holder.overdue = False
            else:
                self.check_overdue_backfill()
            behind_jobs = None
            bench_before = False
            for entry in ordered:
                reason, jobs = self.decide(entry, load, bench_waiting, behind_jobs, bench_before)
                if reason is None and self.grant(entry, jobs):
                    continue
                entry.reason = reason or "bench.lock taken by someone else"
                if behind_jobs is None and entry.kind in COUNTED and entry.waits_for:
                    behind_jobs = entry
                bench_before = bench_before or entry.kind == "bench"
            self.cond.notify_all()

    def decide(self, entry, load, bench_waiting, behind_jobs, bench_before):
        """(reason the entry waits, None) or (None, granted jobs). bench_waiting is the first waiting
        benchmark or None; entry.eta and entry.backfill_end are set on the way where known."""
        entry.eta = None
        entry.backfill_end = None
        entry.waits_for = None
        bench_holder = next((h for h in self.holders.values() if h.kind == "bench" and not self.own_slot(entry, h)), None)
        if bench_holder is not None:
            entry.eta = self.expected_end(bench_holder)
            return "a benchmark holds the machine", 0
        # bench.lock of a current holder is the broker's own; the holder check above covers it.
        own = {h.holder_pid() for h in self.holders.values() if h.lock_owner}
        blocked = foreign_lock(self.dir, entry.ignore | own, entry.kind != "light")
        if blocked:
            return blocked, 0
        if entry.kind == "light":
            return None, 1
        if entry.kind == "bench":
            return self.decide_bench(entry, load, bench_before)
        if bench_waiting is not None and not entry.inside_slot:
            reason = self.backfill_refusal(entry, bench_waiting)
            if reason:
                entry.eta = self.after_bench(bench_waiting)
                return reason, 0
        if entry.kind == "gate":
            return None, 0
        cap = None
        if behind_jobs is not None:
            reason = self.behind_refusal(entry, behind_jobs)
            if reason:
                return reason, 0
            cap = entry.min_jobs()
        gate = self.gate_load()
        reason = self.load_gate(entry, gate)
        if reason:
            return reason, 0
        free = self.budget - self.used_jobs()
        need = min(entry.min_jobs(), self.budget)
        if free < need:
            entry.eta = self.first_end()
            entry.waits_for = "budget"
            return f"needs ≥{need} jobs, budget allows {max(0, free)} ({self.budget - free} of {self.budget} in use)", 0
        reason = self.room_refusal(entry, gate, need)
        if reason:
            entry.waits_for = "load"
            return reason, 0
        return None, self.scaled_jobs(entry, free, gate, cap)

    def held_threads(self):
        """Threads the held build/test/heavy slots run about: a build job one compiler (2 threads), a
        test or heavy job QC_SLOT_THREADS_PER_JOB (ctest jobs x test threads)."""
        return sum(h.jobs * (BUILD_THREADS_PER_JOB if h.kind == "build" else self.threads_per_job)
                   for h in self.holders.values() if h.kind in COUNTED)

    def short_held(self):
        """Jobs held by short build/test slots (minimum below LONG_MIN)."""
        return sum(h.jobs for h in self.holders.values() if h.kind in COUNTED and not h.is_long())

    def behind_refusal(self, entry, long):
        """Why a request waits behind an earlier one that waits for jobs, or None (E-892).

        Requests do not overtake one that came first and waits for jobs, with one exception: behind a
        long request (minimum LONG_MIN or more) a short one starts with its minimum (1 job) as long as
        the short slots then still leave the long one its minimum of the budget, and until the long
        one has waited QC_SLOT_LOAD_WAIT for the load (then the load drains for it).
        """
        base = f"behind #{long.id} {long.kind} '{long.who}', which came first and waits for jobs"
        if entry.is_long() or not long.is_long():
            return base
        need = min(long.min_jobs(), self.budget)
        cap = entry.min_jobs()
        if self.budget - self.short_held() - cap < need:
            return f"{base} (needs ≥{need}; short slots keep that much of the budget free)"
        waited = time.monotonic() - long.since_mono
        if long.waits_for == "load" and waited >= self.load_wait:
            return f"{base} for {duration(waited)}; no new slots until it starts (QC_SLOT_LOAD_WAIT)"
        if self.budget - self.used_jobs() < cap:
            return base
        return None

    def room_refusal(self, entry, gate, need):
        """Why a request waits because the load leaves fewer cores than its minimum, or None (E-892).

        A request never starts with fewer jobs than its minimum. While held build/test/heavy slots
        explain the load the wait has no limit (they end and the load drops); against foreign load it
        lasts at most QC_SLOT_LOAD_WAIT, like the load gate, and then grants the minimum.
        """
        room = int(self.cores - gate)
        # A small machine never has 4 cores free beside its own load: there half the cores suffice.
        need = min(need, max(1, self.cores // 2))
        if need <= 1 or room >= need or entry.escaped:
            return None
        what = f"needs ≥{need} jobs, load {gate:.1f} allows {max(0, room)}"
        held = self.used_jobs()
        if held and self.held_threads() >= gate - (self.cores - need):
            return f"{what}; the held slots end and the load drops"
        waited = time.monotonic() - max(entry.since_mono, self.load_wait_since)
        if waited < self.load_wait:
            source = "foreign load beyond the held jobs" if held else "nothing else held"
            return f"{what}; {source}, granted after {self.load_wait:g}s"
        entry.note = f"granted {need} jobs at load {gate:.1f} after waiting {waited:.0f}s (QC_SLOT_LOAD_WAIT)"
        entry.escaped = True
        return None

    def expected_end(self, holder):
        """Expected end (wall time) of a held slot from the duration history, None when unknown;
        a slot that already ran longer than expected ends 'now'."""
        expected = self.history.estimate(holder.kind, holder.who)
        if expected is None:
            return None
        return max(time.time(), (holder.granted_at or holder.since) + expected)

    def counted_ends(self):
        return [self.expected_end(h) for h in self.holders.values() if h.kind in COUNTED]

    def first_end(self):
        """When the first build/test/heavy slot is expected to end (jobs free up), or None."""
        ends = [e for e in self.counted_ends() if e is not None]
        return min(ends) if ends else None

    def last_end(self):
        """When the last build/test/heavy slot is expected to end, None when one is unknown."""
        ends = self.counted_ends()
        if not ends:
            return time.time()
        return None if None in ends else max(ends)

    def after_bench(self, bench):
        """Expected end of the waiting benchmark (its start plus its usual duration), or None."""
        expected = self.history.estimate(bench.kind, bench.who)
        start = bench.eta or self.last_end()
        return start + expected if start is not None and expected is not None else None

    def backfill_window(self, bench):
        """Seconds until the slots granted before the benchmark arrived are expected to end (the
        benchmark waits for them anyway), 0 for ones already over time or unknown; None when none of
        them runs any more."""
        before = [h for h in self.holders.values()
                  if h.kind in COUNTED and h.granted_mono is not None and h.granted_mono <= bench.since_mono]
        if not before:
            return None
        ends = [self.expected_end(h) or time.time() for h in before]
        return max(ends) - time.time()

    def backfill_refusal(self, entry, bench):
        """Why a request does not backfill while the benchmark waits, or None (entry.backfill_end set).

        A build or test slot backfills when its longest recent duration (DurationHistory.pessimistic)
        ends before the slots
        that were running when the benchmark arrived are expected to end, plus QC_SLOT_BACKFILL_GRACE;
        never once those slots ended or the benchmark waited QC_SLOT_BACKFILL_MAX_WAIT seconds.
        """
        base = f"benchmark #{bench.id} waits"
        if entry.kind == "gate":
            return f"{base}; waiting until it ran"
        if entry.kind not in BACKFILL_KINDS:
            return f"{base}; no new {entry.kind} slots until it ran (only short build/test slots backfill)"
        if not self.backfill:
            return f"{base}; no new slots until it ran (QC_SLOT_BACKFILL=0)"
        waited = time.monotonic() - bench.since_mono
        if waited >= self.backfill_max_wait:
            return f"{base} for {duration(waited)}; backfill stopped after {self.backfill_max_wait:g}s (QC_SLOT_BACKFILL_MAX_WAIT)"
        window = self.backfill_window(bench)
        if window is None:
            return f"{base}; the slots it waited for ended, nothing backfills"
        if (entry.kind, entry.who) in self.backfill_blocked:
            return f"{base}; the last backfill of {entry.kind} '{entry.who}' ran over time, no backfill until it has a fresh duration"
        expected = self.history.pessimistic(entry.kind, entry.who)
        if expected is None:
            return f"{base}; fewer than 2 past durations of {entry.kind} '{entry.who}', so it does not backfill"
        if expected > window + self.backfill_grace:
            return (f"{base}; takes up to {duration(expected)}, longer than the running slots "
                    f"(end ~{clock_text(time.time() + window)}) + {self.backfill_grace:g}s (QC_SLOT_BACKFILL_GRACE)")
        entry.backfill_end = time.time() + expected
        entry.backfill_bench = bench.id
        return None

    def check_overdue_backfill(self):
        """Marks backfilled slots past their expected end plus the grace (logged once; they keep
        running) and stops backfilling their label until it has a fresh duration."""
        now = time.time()
        for holder in self.holders.values():
            if holder.backfill_end and not holder.overdue and now > holder.backfill_end + self.backfill_grace:
                holder.overdue = True
                self.backfill_blocked.add((holder.kind, holder.who))
                self.log(f"#{holder.id} {holder.kind} who={holder.who!r}: backfill over time (expected end "
                         f"{clock_text(holder.backfill_end)}), delays benchmark #{holder.backfill_bench}")

    def load_gate(self, entry, gate):
        """Why a build/test/heavy request waits for the load to drop, or None.

        While held build/test/heavy slots explain the load (their jobs at least load - cores), the gate
        holds without limit: they end and the load drops (a job runs about QC_SLOT_THREADS_PER_JOB
        threads, default 6). Otherwise (none held, or load mostly from
        processes outside the broker) it holds at most QC_SLOT_LOAD_WAIT, counted from arrival or from
        the last release of such a slot or the last escape, so foreign load never serialises the
        queue for ever and at most one request escapes per period.
        """
        entry.note = ""
        entry.escaped = False
        if gate < self.load_limit:
            return None
        pending = self.pending_jobs()
        what = f"load {gate:.1f}" + (f" (with {pending:g} jobs just granted)" if pending else "")
        held = self.used_jobs()
        if held and held * self.threads_per_job >= gate - self.cores:
            return f"{what} at or above {self.load_limit:g} (QC_SLOT_LOAD_LIMIT)"
        waited = time.monotonic() - max(entry.since_mono, self.load_wait_since)
        if waited < self.load_wait:
            source = "foreign load beyond the held jobs" if held else "nothing else held"
            return f"{what} at or above {self.load_limit:g} (QC_SLOT_LOAD_LIMIT); {source}, granted after {self.load_wait:g}s"
        entry.note = f"granted at load {gate:.1f} (limit {self.load_limit:g}) after waiting {waited:.0f}s (QC_SLOT_LOAD_WAIT)"
        entry.escaped = True
        return None

    def scaled_jobs(self, entry, free, gate, cap=None):
        """min(wish, free jobs, cores - load), but at least the request's minimum: a loaded machine
        gets fewer jobs; behind a waiting long request at most cap (the short request's minimum)."""
        room = max(entry.min_jobs(), int(self.cores - gate))
        jobs = min(entry.wish, free, room)
        if room < min(entry.wish, free):
            self.add_note(entry, f"jobs lowered to {jobs} at load {gate:.1f}")
        if cap is not None and jobs > cap:
            jobs = cap
            self.add_note(entry, f"{jobs} job(s) while a long request waits for its minimum")
        return jobs

    @staticmethod
    def add_note(entry, text):
        entry.note = f"{entry.note}; {text}" if entry.note else text

    @staticmethod
    def own_slot(entry, holder):
        """Whether the request comes from inside the holder's slot (its command or a child of it)."""
        if not entry.inside_slot:
            return False
        return entry.slot_id == holder.id or holder.holder_pid() in entry.ignore

    def decide_bench(self, entry, load, bench_before):
        if bench_before:
            return "behind an earlier benchmark", 0
        running = [h for h in self.holders.values() if h.kind in COUNTED]
        if running:
            entry.eta = self.last_end()
            backfilled = [h for h in running if h.backfill_end]
            extra = f", {len(backfilled)} of them backfill" if backfilled else ""
            return f"{len(running)} build/test/verify slot(s) still running{extra}", 0
        waited = time.monotonic() - entry.since_mono
        if load >= self.bench_load:
            if waited < self.load_wait:
                return f"load not below {self.bench_load:g} (QC_SLOT_BENCH_LOAD)", 0
            entry.note = f"granted at load {load:.1f} (not below {self.bench_load:g}) after waiting {waited:.0f}s (QC_SLOT_LOAD_WAIT)"
        return None, self.cores

    def grant(self, entry, jobs):
        if entry.kind == "bench" and not self.take_bench_lock(entry):
            return False
        now = time.time()
        self.queue.remove(entry)
        entry.jobs = jobs
        entry.granted_at = now
        entry.granted_mono = time.monotonic()
        if entry.escaped:
            self.load_wait_since = entry.granted_mono
        entry.result = "granted"
        entry.reason = ""
        if entry.kind != "gate":
            self.holders[entry.id] = entry
            if entry.lease == "hold":
                entry.expires = now + entry.hold
        self.last_activity = now
        if entry.backfill_end:
            backfill = f"backfill while a benchmark waits, expected end ~{clock_text(entry.backfill_end)}"
            entry.note = f"{entry.note}; {backfill}" if entry.note else backfill
        self.log(f"grant #{entry.id} {entry.kind} jobs={jobs} who={entry.who!r} after {now - entry.since:.0f}s"
                 + (f"; {entry.note}" if entry.note else ""))
        entry.done.set()
        return True

    def take_bench_lock(self, entry):
        path = lock_paths(self.dir)[0]
        if os.path.isdir(path) and lock_holder(path, set()) is None:
            remove_stale_lock(path, self.log)
        try:
            os.makedirs(self.dir, exist_ok=True)
            os.mkdir(path)
        except OSError:
            return False
        self.write_bench_owner(entry)
        return True

    def write_bench_owner(self, entry):
        path = lock_paths(self.dir)[0]
        owner = f"bench-slot.sh pid={entry.holder_pid()} dir={entry.cwd} who={entry.who} tool=qc-slot.py"
        with open(os.path.join(path, "owner"), "w", encoding="utf-8") as handle:
            handle.write(owner + "\n")
        entry.lock_owner = owner

    def follow_child(self, entry, why):
        """The client of a run slot is gone: the slot follows its command until that ends."""
        with self.cond:
            if entry.id not in self.holders:
                return
            if not entry.child_pid or not pid_alive(entry.child_pid):
                self.release(entry.id, why)
                return
            entry.lease = "pid"
            entry.pid = entry.child_pid
            if entry.lock_owner:
                self.write_bench_owner(entry)
            self.log(f"#{entry.id} {entry.kind} who={entry.who!r}: {why}; the slot follows command pid {entry.pid}")

    def drop_bench_lock(self, entry, ended=False):
        """Removes the slot's bench.lock, unless its owner (the benchmark command) still runs: then
        old scripts keep seeing it, and it turns stale once that process ends."""
        if not entry.lock_owner:
            return
        path = lock_paths(self.dir)[0]
        match = re.search(r"\bpid=(\d+)", entry.lock_owner)
        owner_pid = int(match.group(1)) if match else None
        if not ended and owner_pid and owner_pid != os.getpid() and pid_alive(owner_pid):
            self.log(f"#{entry.id}: bench.lock kept, its owner pid {owner_pid} still runs")
            return
        try:
            with open(os.path.join(path, "owner"), encoding="utf-8") as handle:
                if handle.read().strip() != entry.lock_owner:
                    return
            os.remove(os.path.join(path, "owner"))
            os.rmdir(path)
        except OSError:
            pass

    def tick(self):
        """Releases holders whose process ended or whose hold expired, times out waiters, regrants."""
        with self.cond:
            now = time.time()
            for entry in list(self.holders.values()):
                if entry.lease == "conn" and entry.client_pid and not pid_alive(entry.client_pid):
                    self.follow_child(entry, f"client pid {entry.client_pid} ended")
                elif entry.lease == "pid" and entry.pid and not pid_alive(entry.pid):
                    self.release(entry.id, f"holder pid {entry.pid} ended")
                elif entry.expires and now > entry.expires:
                    self.release(entry.id, "hold time over")
            for entry in list(self.queue):
                if entry.deadline and now > entry.deadline:
                    self.cancel(entry, "timeout")
            self.schedule()
            busy = self.holders or self.queue or self.connections
            if busy:
                self.last_activity = now
            elif now - self.last_activity > self.idle_seconds:
                self.log(f"idle for {self.idle_seconds:g}s, ending")
                self.stopping = True

    def snapshot(self):
        with self.cond:
            now = time.time()
            locks = {}
            for path in lock_paths(self.dir):
                locks[os.path.basename(path)] = lock_holder(path, set())
            short, from_window = self.monitor.measure()
            return {
                "version": VERSION, "pid": os.getpid(), "up": round(now - self.started), "load": round(machine_load(), 2),
                "cores": self.cores, "budget": self.budget, "used": self.used_jobs(), "bench_load": self.bench_load,
                "load_short": round(short, 2), "load_short_window": from_window, "pending": self.pending_jobs(),
                "load_limit": self.load_limit, "load_window": self.load_window,
                "backfill": {"enabled": self.backfill, "grace": self.backfill_grace, "max_wait": self.backfill_max_wait,
                             "history": self.history_loaded},
                "locks": locks,
                "holders": [h.view(now) for h in sorted(self.holders.values(), key=lambda e: e.id)],
                "queue": [e.view(now) for e in sorted(self.queue, key=lambda e: (PRIORITY[e.kind], e.id))],
            }


def peer_closed(sock):
    readable, _, _ = select.select([sock], [], [], 0)
    if not readable:
        return False
    try:
        return sock.recv(1, socket.MSG_PEEK) == b""
    except OSError:
        return True


def lock_within(handle, seconds):
    """Takes the exclusive flock of the pid file, waiting a little for a broker that is ending."""
    deadline = time.time() + seconds
    while True:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except OSError:
            if time.time() >= deadline:
                return False
            time.sleep(0.1)


class Server:
    """Accepts clients on the Unix socket, one thread per connection."""

    def __init__(self, directory):
        self.dir = directory
        self.socket_path = os.path.join(directory, "qc-slot.sock")
        self.log_lock = threading.Lock()
        self.log_path = os.path.join(directory, "qc-slot.log")
        self.broker = Broker(directory, self.log)

    def log(self, text):
        line = time.strftime("%Y-%m-%d %H:%M:%S ") + text + "\n"
        with self.log_lock:
            with open(self.log_path, "a", encoding="utf-8") as handle:
                handle.write(line)

    def rotate_log(self):
        try:
            if os.path.getsize(self.log_path) > 1_000_000:
                os.replace(self.log_path, self.log_path + ".1")
        except OSError:
            pass

    def run(self):
        os.makedirs(self.dir, exist_ok=True)
        pid_file = open(os.path.join(self.dir, "qc-slot.pid"), "a+", encoding="utf-8")
        if not lock_within(pid_file, 3):
            print("qc-slot: a broker already runs for " + self.dir, file=sys.stderr)
            return 0
        pid_file.truncate(0)
        pid_file.write(f"{os.getpid()}\n")
        pid_file.flush()
        self.rotate_log()
        listener = self.listen()
        if listener is None:
            message = f"socket path longer than {SOCKET_PATH_MAX} bytes: {self.socket_path}; set QC_SLOT_DIR to a shorter folder"
            self.log(message)
            print("qc-slot: " + message, file=sys.stderr)
            return 2
        self.log(f"broker pid {os.getpid()} started: cores={self.broker.cores} budget={self.broker.budget} "
                 f"bench_load={self.broker.bench_load:g} load_limit={self.broker.load_limit:g}")
        threading.Thread(target=self.ticker, daemon=True).start()
        threading.Thread(target=self.sampler, daemon=True).start()
        listener.settimeout(0.25)
        while not self.broker.stopping:
            try:
                conn, _ = listener.accept()
            except socket.timeout:
                continue
            conn.settimeout(None)
            threading.Thread(target=self.handle, args=(conn,), daemon=True).start()
        listener.close()
        os.unlink(self.socket_path)
        self.log("broker ended")
        return 0

    def listen(self):
        if len(self.socket_path.encode()) > SOCKET_PATH_MAX:
            return None
        if os.path.exists(self.socket_path):
            os.unlink(self.socket_path)
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(self.socket_path)
        os.chmod(self.socket_path, 0o600)
        listener.listen(64)
        return listener

    def ticker(self):
        while not self.broker.stopping:
            time.sleep(self.broker.tick_seconds)
            try:
                self.broker.tick()
            except Exception as error:  # noqa: BLE001 - the ticker must survive any single failure
                try:
                    self.log(f"tick failed: {error!r}")
                except OSError:
                    print(f"qc-slot: tick failed: {error!r}", file=sys.stderr)

    def sampler(self):
        """Samples the load on a fixed timer, independent of QC_SLOT_TICK (it must see every update)."""
        while not self.broker.stopping:
            time.sleep(min(LOAD_SAMPLE_SECONDS, self.broker.tick_seconds))
            try:
                self.broker.monitor.sample()
            except Exception as error:  # noqa: BLE001 - sampling must survive any single failure
                try:
                    self.log(f"load sample failed: {error!r}")
                except OSError:
                    print(f"qc-slot: load sample failed: {error!r}", file=sys.stderr)

    def handle(self, conn):
        with self.broker.cond:
            self.broker.connections += 1
        try:
            with conn:
                self.dispatch(conn, LineReader(conn))
        except OSError:
            pass
        finally:
            with self.broker.cond:
                self.broker.connections -= 1

    def dispatch(self, conn, reader):
        request = reader.read() or {}
        op = request.get("op")
        if op == "acquire":
            self.acquire(conn, reader, request)
        elif op == "adopt":
            self.adopt(conn, reader, request)
        elif op == "status":
            send_json(conn, self.broker.snapshot())
        elif op == "release":
            self.release(conn, request)
        elif op == "stop":
            self.stop(conn, request)
        else:
            send_json(conn, {"ok": False, "reason": f"unknown op {op!r}"})

    def release(self, conn, request):
        if request.get("id"):
            entry = self.broker.release(int(request["id"]), "released by id")
            send_json(conn, {"ok": entry is not None, "released": [entry.id] if entry else []})
        else:
            released = self.broker.release_who(str(request.get("who")), "released by label")
            send_json(conn, {"ok": bool(released), "released": released})

    def stop(self, conn, request):
        with self.broker.cond:
            if any(h.kind == "bench" for h in self.broker.holders.values()):
                send_json(conn, {"ok": False, "reason": "a benchmark holds a slot; not even --force stops the broker now"})
                return
            busy = self.broker.holders or self.broker.queue
            if busy and not request.get("force"):
                send_json(conn, {"ok": False, "reason": "slots are held or awaited (use --force)"})
                return
            # Stopping first: releasing a holder must not grant a waiting request any more.
            self.broker.stopping = True
            for ident in list(self.broker.holders):
                self.broker.release(ident, "broker stopped")
            for entry in list(self.broker.queue):
                self.broker.cancel(entry, "broker stopped")
        send_json(conn, {"ok": True})

    def acquire(self, conn, reader, request):
        if request.get("kind") not in KINDS + ("gate",):
            send_json(conn, {"ok": False, "reason": f"unknown kind {request.get('kind')!r}"})
            return
        if self.broker.stopping:
            # The client asks again and reaches the next broker.
            send_json(conn, {"ok": False, "reason": "broker stopped"})
            return
        entry = self.broker.submit(request)
        told = None
        while not entry.done.wait(0.2):
            if peer_closed(conn):
                self.broker.cancel(entry, "client gone while waiting")
                return
            reason = entry.reason
            if reason != told:
                send_json(conn, {"waiting": reason, "id": entry.id})
                told = reason
        if entry.result != "granted":
            send_json(conn, {"ok": False, "reason": entry.result, "id": entry.id})
            return
        grant = {"ok": True, "id": entry.id, "kind": entry.kind, "jobs": entry.jobs,
                 "waited": round(entry.granted_at - entry.since, 1), "holder_pid": entry.holder_pid(),
                 "note": entry.note, "version": VERSION}
        try:
            send_json(conn, grant)
        except OSError:
            self.broker.release(entry.id, "client gone at grant")
            return
        if entry.lease == "conn" and entry.kind != "gate":
            self.hold_connection(entry, reader)

    def adopt(self, conn, reader, request):
        """A run client whose broker ended registers its running slot again, without queueing."""
        if request.get("kind") not in KINDS or self.broker.stopping:
            send_json(conn, {"ok": False, "reason": "broker stopped"})
            return
        entry = self.broker.adopt(request)
        send_json(conn, {"ok": True, "id": entry.id, "kind": entry.kind, "jobs": entry.jobs})
        self.hold_connection(entry, reader)

    def hold_connection(self, entry, reader):
        """The lease of a run slot: released on request; a lost client hands it to its command."""
        while True:
            message = reader.read()
            if message is None:
                self.broker.follow_child(entry, "client connection closed")
                return
            if message.get("op") == "child" and str(message.get("pid", "")).isdigit():
                with self.broker.cond:
                    entry.child_pid = int(message["pid"])
                continue
            if message.get("op") == "release":
                # The command ended: its bench.lock goes too, although the client still lives.
                code = message.get("exit")
                exit_code = code if isinstance(code, int) and not isinstance(code, bool) else None
                self.broker.release(entry.id, "released", ended=True, exit_code=exit_code)
                return


# ---- client ----

def socket_path():
    return os.path.join(slot_dir(), "qc-slot.sock")


def try_connect():
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sock.connect(socket_path())
        return sock
    except OSError:
        sock.close()
        return None


def start_daemon():
    directory = slot_dir()
    os.makedirs(directory, exist_ok=True)
    with open(os.path.join(directory, "qc-slot.log"), "a", encoding="utf-8") as log:
        subprocess.Popen([sys.executable, os.path.abspath(__file__), "serve"], cwd=directory,
                         stdin=subprocess.DEVNULL, stdout=log, stderr=log, start_new_session=True,
                         close_fds=True)


def connect(autostart=True):
    """A connection to the broker, starting it when needed; None when disabled or unreachable."""
    if os.environ.get("QC_SLOT", "1") == "0":
        return None
    if len(socket_path().encode()) > SOCKET_PATH_MAX:
        print(f"qc-slot: socket path longer than {SOCKET_PATH_MAX} bytes ({socket_path()}); "
              "set QC_SLOT_DIR to a shorter folder", file=sys.stderr)
        return None
    sock = try_connect()
    if sock or not autostart:
        return sock
    try:
        start_daemon()
    except OSError as error:
        print(f"qc-slot: cannot start the broker: {error}", file=sys.stderr)
        return None
    deadline = time.time() + 5
    while time.time() < deadline:
        time.sleep(0.05)
        sock = try_connect()
        if sock:
            return sock
    return None


def ignored_pids():
    """Locks of these processes never block this client: the calling script and lock holders above it."""
    pids = [os.getppid()]
    for name in ("QC_QUIET_LOCK_PID", "QC_BENCH_LOCK_PID"):
        value = os.environ.get(name, "")
        if value.isdigit():
            pids.append(int(value))
    return pids


def env_wish():
    wishes = [int(os.environ[n]) for n in ("QC_BUILD_JOBS", "QC_TEST_JOBS") if os.environ.get(n, "").isdigit()]
    return max(wishes) if wishes else None


def request_slot(args, kind, lease):
    """(socket, grant) once granted; (None, None) when unavailable; (sock, None) on timeout.

    A broker that ends (idle, restarted) before the grant closes the connection; the request is then
    queued again at a reachable or newly started broker, never run without a slot.
    """
    deadline = time.time() + args.timeout if args.timeout else None
    for attempt in range(20):
        sock = connect()
        if sock is None:
            return None, None
        remaining = max(0.1, deadline - time.time()) if deadline else 0
        result = send_request(sock, args, kind, lease, remaining)
        if result is not None:
            return result
        sock.close()
        if deadline and time.time() >= deadline:
            print(f"qc-slot: {kind} '{args.who}' not granted: timeout", file=sys.stderr)
            return sock, None
        print("qc-slot: the broker closed the connection before a grant; asking again", file=sys.stderr)
        time.sleep(0.3)
    print("qc-slot: the broker keeps closing the connection", file=sys.stderr)
    return None, None


def send_request(sock, args, kind, lease, timeout):
    """(sock, grant or None), or None when the connection closed before the answer."""
    request = {"op": "acquire", "kind": kind, "who": args.who, "client_pid": os.getpid(), "lease": lease,
               "pid": getattr(args, "pid", None), "hold": getattr(args, "hold", None), "cwd": os.getcwd(),
               "jobs": getattr(args, "jobs", None) or env_wish(),
               "jobs_explicit": bool(getattr(args, "jobs", None)), "min_jobs": getattr(args, "min_jobs", None),
               "timeout": timeout, "ignore": ignored_pids(),
               "inside_slot": bool(os.environ.get("QC_SLOT_ID")), "slot_id": os.environ.get("QC_SLOT_ID")}
    try:
        send_json(sock, request)
    except OSError:
        return None
    reader = LineReader(sock)
    while True:
        message = reader.read()
        if message is None:
            return None
        if "waiting" in message:
            print(f"qc-slot: #{message['id']} {kind} '{args.who}' waits: {message['waiting']}", file=sys.stderr)
            continue
        if message.get("ok"):
            warning = version_warning(message.get("version"), False)
            if warning:
                print(warning, file=sys.stderr)
            return sock, message
        if message.get("reason") == "broker stopped":
            return None
        print(f"qc-slot: {kind} '{args.who}' not granted: {message.get('reason')}", file=sys.stderr)
        return sock, None


def job_env(grant):
    env = {"QC_SLOT_ID": str(grant["id"]), "QC_SLOT_KIND": grant["kind"], "QC_SLOT_JOBS": str(grant["jobs"])}
    if grant["kind"] in COUNTED or grant["kind"] == "bench":
        for name in ("QC_BUILD_JOBS", "QC_TEST_JOBS"):
            own = os.environ.get(name, "")
            jobs = grant["jobs"]
            env[name] = str(min(int(own), jobs) if own.isdigit() and int(own) > 0 else jobs)
    if grant["kind"] == "bench":
        env["QC_BENCH_LOCK_PID"] = str(grant["holder_pid"])
    return env


def wait_for_locks(kind, timeout):
    """Fallback without broker: waits while a foreign lock file blocks this kind. True when free."""
    poll = max(0.05, number("QC_SLOT_POLL", 10))
    deadline = time.time() + timeout if timeout else None
    told = None
    while True:
        blocked = foreign_lock(slot_dir(), set(ignored_pids()), kind != "light")
        if not blocked:
            return True
        if blocked != told:
            print(f"qc-slot (no broker): waiting: {blocked}", file=sys.stderr)
            told = blocked
        if deadline and time.time() >= deadline:
            print(f"qc-slot (no broker): still blocked after {timeout:g}s: {blocked}", file=sys.stderr)
            return False
        time.sleep(poll)


def take_fallback_bench_lock(who):
    path = lock_paths(slot_dir())[0]
    os.makedirs(slot_dir(), exist_ok=True)
    try:
        os.mkdir(path)
    except FileExistsError:
        pass
    owner = f"bench-slot.sh pid={os.getpid()} dir={os.getcwd()} who={who} tool=qc-slot.py"
    with open(os.path.join(path, "owner"), "w", encoding="utf-8") as handle:
        handle.write(owner + "\n")
    return path, owner


def drop_fallback_bench_lock(path, owner):
    try:
        with open(os.path.join(path, "owner"), encoding="utf-8") as handle:
            if handle.read().strip() == owner:
                os.remove(os.path.join(path, "owner"))
                os.rmdir(path)
    except OSError:
        pass


def unavailable(args):
    why = "disabled (QC_SLOT=0)" if os.environ.get("QC_SLOT") == "0" else "not reachable"
    if args.no_fallback:
        print(f"qc-slot: broker {why}", file=sys.stderr)
        return True
    print(f"qc-slot: broker {why}; using the lock files only", file=sys.stderr)
    return False


def run_child(command, env, started=None):
    """Runs the command, passing INT, TERM and HUP on; returns its exit code in shell form.

    The handlers are in place before the command starts; a signal that arrives before it exists is
    passed on right after the start, so no signal leaves the command behind as an orphan. started(pid)
    is called once the command runs.
    """
    child = None
    pending = []

    def forward(signum, _frame):
        if child is None:
            pending.append(signum)
        else:
            child.send_signal(signum)

    for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(signum, forward)
    try:
        child = subprocess.Popen(command, env=env)
    except OSError as error:
        print(f"qc-slot: cannot run {command[0]}: {error}", file=sys.stderr)
        return 127
    for signum in pending:
        child.send_signal(signum)
    if started:
        started(child.pid)
    code = child.wait()
    return code if code >= 0 else 128 - code


class RunLease:
    """The connection that holds a run slot. When the broker ends while the command runs, the lease
    says so loudly and registers the running slot with the next broker (op adopt)."""

    def __init__(self, sock, args, grant):
        self.sock = sock
        self.args = args
        self.grant = grant
        self.child_pid = None
        self.granted_at = time.time()
        self.done = False
        self.lock = threading.Lock()
        threading.Thread(target=self.watch, daemon=True).start()

    def started(self, pid):
        with self.lock:
            self.child_pid = pid
            sock = self.sock
        try:
            send_json(sock, {"op": "child", "pid": pid})
        except OSError:
            pass

    def watch(self):
        while True:
            with self.lock:
                sock = self.sock
            LineReader(sock).read()
            with self.lock:
                if self.done:
                    return
            print(f"qc-slot: WARNING: the broker ended while #{self.grant['id']} {self.args.kind} '{self.args.who}' runs; "
                  "registering the slot with a new broker", file=sys.stderr)
            if not self.adopt():
                print("qc-slot: WARNING: no broker took the slot; the command goes on without one", file=sys.stderr)
                return

    def adopt(self):
        for _ in range(20):
            time.sleep(0.3)
            sock = connect()
            if sock is None:
                return False
            with self.lock:
                request = {"op": "adopt", "kind": self.args.kind, "who": self.args.who, "client_pid": os.getpid(),
                           "lease": "conn", "cwd": os.getcwd(), "jobs": self.grant["jobs"], "child_pid": self.child_pid,
                           "min_jobs": getattr(self.args, "min_jobs", None),
                           "id": self.grant["id"], "since": self.granted_at}
            try:
                send_json(sock, request)
            except OSError:
                sock.close()
                continue
            answer = LineReader(sock).read()
            if answer and answer.get("ok"):
                with self.lock:
                    self.sock = sock
                return True
            sock.close()
        return False

    def release(self, code):
        """Ends the lease; the exit code tells the broker whether the run's duration counts."""
        with self.lock:
            self.done = True
            sock = self.sock
        try:
            send_json(sock, {"op": "release", "exit": code})
        except OSError:
            pass
        sock.close()


def covered(slot_id, outer, inner):
    """Whether work of kind inner may run inside the held slot of kind outer without asking again."""
    if not slot_id:
        return False
    if outer == "bench":
        return True
    if outer in HEAVY:
        return inner in COUNTED
    if outer in ("build", "test"):
        return inner in ("build", "test")
    return False


def nesting_refusal(kind):
    """Error text when a bench or heavy request comes from inside a slot that does not cover it.

    Queueing it would deadlock: a bench waits for every build/test/heavy slot (its own parent among
    them), a heavy request inside a build or test slot waits behind requests that wait for the outer
    slot's jobs. None when the request may go to the broker.
    """
    slot_id = os.environ.get("QC_SLOT_ID")
    outer = os.environ.get("QC_SLOT_KIND", "")
    if not slot_id or covered(slot_id, outer, kind) or kind not in ("bench",) + HEAVY:
        return None
    right = "bench" if kind == "bench" else "verify (or tsan/asan)"
    return (f"qc-slot: a {kind} slot cannot be requested inside {outer or 'a'} slot #{slot_id}; it would wait "
            f"for that slot. Start the outer work with --kind {right}, or run this outside the slot.")


def command_run(args):
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        print("qc-slot run: no command after --", file=sys.stderr)
        return 2
    if covered(os.environ.get("QC_SLOT_ID"), os.environ.get("QC_SLOT_KIND"), args.kind):
        return run_child(command, dict(os.environ))
    refusal = nesting_refusal(args.kind)
    if refusal:
        print(refusal, file=sys.stderr)
        return 2
    sock, grant = request_slot(args, args.kind, "conn")
    if sock is None:
        if unavailable(args):
            return EXIT_UNAVAILABLE
        return run_fallback(args, command)
    if grant is None:
        return 1
    note = f" ({grant['note']})" if grant.get("note") else ""
    print(f"qc-slot: #{grant['id']} {args.kind} '{args.who}' granted {grant['jobs']} jobs after {grant['waited']:g}s{note}",
          file=sys.stderr)

    lease = RunLease(sock, args, grant)
    code = run_child(command, {**os.environ, **job_env(grant)}, lease.started)
    lease.release(code)
    return code


def run_fallback(args, command):
    if not wait_for_locks(args.kind, args.timeout):
        return 1
    jobs = args.jobs or env_wish() or default_wish(args.kind, core_count())
    grant = {"id": "fallback", "kind": args.kind, "jobs": jobs, "holder_pid": os.getpid()}
    lock = take_fallback_bench_lock(args.who) if args.kind == "bench" else None
    try:
        return run_child(command, {**os.environ, **job_env(grant)})
    finally:
        if lock:
            drop_fallback_bench_lock(*lock)


def command_acquire(args):
    if args.kind == "bench" and not args.pid and args.hold > BENCH_HOLD_MAX:
        print(f"qc-slot acquire: a bench slot needs --pid or --hold of at most {BENCH_HOLD_MAX}s; "
              "agents use `qc-slot.py run --kind bench ... -- <command>`", file=sys.stderr)
        return 2
    refusal = nesting_refusal(args.kind)
    if refusal:
        print(refusal, file=sys.stderr)
        return 2
    lease = "pid" if args.pid else "hold"
    sock, grant = request_slot(args, args.kind, lease)
    if sock is None:
        if unavailable(args):
            return EXIT_UNAVAILABLE
        if not wait_for_locks(args.kind, args.timeout):
            return 1
        jobs = args.jobs or env_wish() or default_wish(args.kind, core_count())
        grant = {"id": "fallback", "kind": args.kind, "jobs": jobs, "holder_pid": os.getpid()}
    elif grant is None:
        return 1
    else:
        sock.close()
    for name, value in job_env(grant).items():
        print(f"export {name}={value}")
    if grant.get("note"):
        print(f"# note: {grant['note']}")
    print(f"# {args.kind} slot #{grant['id']} for '{args.who}', {grant['jobs']} jobs; "
          f"release: scripts/qc-slot.py release --id {grant['id']}")
    return 0


def command_wait(args):
    sock, grant = request_slot(args, "gate", "conn")
    if sock is None:
        if unavailable(args):
            return EXIT_UNAVAILABLE
        return 0 if wait_for_locks("gate", args.timeout) else 1
    sock.close()
    return 0 if grant else 1


def simple_request(request):
    sock = connect(autostart=False)
    if sock is None:
        return None
    with sock:
        send_json(sock, request)
        return LineReader(sock).read()


def command_release(args):
    if not args.id and not args.who:
        print("qc-slot release: --id or --who", file=sys.stderr)
        return 2
    answer = simple_request({"op": "release", "id": args.id, "who": args.who})
    if answer is None:
        print("qc-slot: no broker running", file=sys.stderr)
        return 1
    print(f"released: {answer.get('released')}")
    return 0 if answer.get("ok") else 1


def command_stop(args):
    answer = simple_request({"op": "stop", "force": args.force})
    if answer is None:
        print("qc-slot: no broker running")
        return 0
    if not answer.get("ok"):
        print(f"qc-slot: {answer.get('reason')}", file=sys.stderr)
        return 1
    return 0


def duration(seconds):
    seconds = int(seconds)
    if seconds < 60:
        return f"{seconds}s"
    if seconds < 3600:
        return f"{seconds // 60}m{seconds % 60:02d}s"
    return f"{seconds // 3600}h{seconds // 60 % 60:02d}m"


def version_warning(version, idle):
    """Warning text when the daemon runs another version than this client, else None."""
    if version == VERSION:
        return None
    if (version or 1) > VERSION:
        return (f"qc-slot: WARNING: the running broker has version {version}, newer than this client ({VERSION}); "
                "its policy applies, but this checkout's scripts/qc-slot.py is old (merge dev for matching options and status)")
    text = (f"qc-slot: WARNING: the running broker has version {version or 1}, this client {VERSION}; "
            "it keeps its old policy until it ends on idle")
    if idle:
        text += "; nothing is held or waiting, so `scripts/qc-slot.py stop` replaces it now"
    return text


def load_line(state):
    """The load part of the status; tolerates an older daemon without the short-term fields."""
    text = f"load {state.get('load', 0):g} (1 min)"
    if "load_short" in state:
        if state.get("load_short_window"):
            text += f", {state['load_short']:g} (last {state.get('load_window', 0):g}s)"
        else:
            text += ", no short-term samples yet (the gate uses the 1-min average)"
        text += f" + {state.get('pending', 0):g} jobs just granted"
    return text


def print_status(state):
    print(f"broker pid {state.get('pid')}, up {duration(state.get('up', 0))}; {load_line(state)}; "
          f"jobs {state.get('used')} of {state.get('budget')} in use ({state.get('cores')} cores)")
    limit = f"build/test/verify need load < {state['load_limit']:g}; " if "load_limit" in state else ""
    print(f"{limit}bench needs load < {state.get('bench_load', 0):g}")
    backfill = state.get("backfill")
    if backfill:
        rule = (f"short build/test slots while a bench waits, ending at most {backfill['grace']:g}s after the slots it "
                f"waits for, for up to {duration(backfill['max_wait'])}" if backfill.get("enabled") else "off (QC_SLOT_BACKFILL=0)")
        print(f"backfill: {rule}; {backfill.get('history', 0)} durations read from the log at start")
    for name, owner in state.get("locks", {}).items():
        print(f"{name}: {owner or 'free'}")
    holders = state.get("holders", [])
    print(f"holders ({len(holders)}):")
    for h in holders:
        clock = time.strftime("%H:%M:%S", time.localtime(h.get("since", 0)))
        print(f"  #{h.get('id', '?'):<4} {h.get('kind', '?'):<6} jobs {h.get('jobs', '?'):<3} min {h.get('min', '?'):<2} pid {h.get('pid', '?'):<7} "
              f"since {clock} ({duration(h.get('held', 0))})  {h.get('who', '?')}")
        if h.get("backfill_overdue"):
            print(f"         overdue backfill delays bench #{h['backfill_overdue']} (expected end ~{clock_text(h['backfill_end'])})")
        elif h.get("backfill_end"):
            print(f"         backfill: ends ~{clock_text(h['backfill_end'])} (expected; a waiting bench waits for it)")
        if h.get("note"):
            print(f"         note: {h['note']}")
    queue = state.get("queue", [])
    print(f"queue ({len(queue)}):")
    for e in queue:
        eta = f" (ETA ~{clock_text(e['eta'])})" if e.get("eta") else ""
        print(f"  #{e.get('id', '?'):<4} {e.get('kind', '?'):<6} min {e.get('min', '?'):<2} waits {duration(e.get('waited', 0)):<8} "
              f"pid {e.get('pid', '?'):<7} {e.get('who', '?')}: {e.get('reason', '?')}{eta}")


def command_status(args):
    state = simple_request({"op": "status"})
    if state is None:
        bench, quiet = lock_paths(slot_dir())
        state = {"running": False, "load": round(machine_load(), 2),
                 "locks": {"bench.lock": lock_holder(bench, set()), "quiet.lock": lock_holder(quiet, set())}}
        if args.json:
            print(json.dumps(state, indent=2))
        else:
            print(f"no broker running; load {state['load']:g}")
            for name, owner in state["locks"].items():
                print(f"{name}: {owner or 'free'}")
        return 0
    state["running"] = True
    warning = version_warning(state.get("version"), not state.get("holders") and not state.get("queue"))
    if warning:
        print(warning, file=sys.stderr)
    if args.json:
        print(json.dumps(state, indent=2))
    else:
        print_status(state)
    return 0


def parse(argv):
    parser = argparse.ArgumentParser(prog="qc-slot.py", description="Slot broker for parallel runs (E-781).")
    sub = parser.add_subparsers(dest="op", required=True)
    sub.add_parser("serve")
    stop = sub.add_parser("stop")
    stop.add_argument("--force", action="store_true")
    status = sub.add_parser("status")
    status.add_argument("--json", action="store_true")
    release = sub.add_parser("release")
    release.add_argument("--id", type=int)
    release.add_argument("--who")
    for name in ("run", "acquire", "wait"):
        command = sub.add_parser(name)
        if name != "wait":
            command.add_argument("--kind", required=True, choices=KINDS)
            command.add_argument("--jobs", type=int)
            command.add_argument("--min-jobs", type=int, help="fewest jobs to start with (default: 4 for "
                                 "tsan/asan/verify/bench, 1 for build/test/light); waits until the load allows it")
        command.add_argument("--who", default=f"{name} {os.path.basename(os.getcwd())}")
        command.add_argument("--timeout", type=float, default=0, help="seconds, 0 = no limit")
        command.add_argument("--no-fallback", action="store_true", help="exit 211 when the broker is unavailable")
        if name == "acquire":
            command.add_argument("--pid", type=int, help="the slot ends with this process")
            command.add_argument("--hold", type=float, default=7200, help="seconds the slot stays without --pid")
        if name == "run":
            command.add_argument("command", nargs=argparse.REMAINDER)
    return parser.parse_args(argv)


def main(argv):
    args = parse(argv)
    if args.op == "serve":
        return Server(slot_dir()).run()
    handlers = {"run": command_run, "acquire": command_acquire, "wait": command_wait, "release": command_release,
                "status": command_status, "stop": command_stop}
    return handlers[args.op](args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
