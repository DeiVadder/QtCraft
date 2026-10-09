#!/usr/bin/env python3
"""Summarises an xctrace Time Profiler export (time-profile table as XML) or a qmlprofiler trace.

Usage:
  profile-report.py time <export.xml> [top]      hottest functions per thread (self and total)
  profile-report.py qml <trace.qtd> [top]        slowest QML bindings, signals and creations

Only reads the given file and prints a text report; used by scripts/profile.sh.
"""

import collections
import re
import sys
import xml.etree.ElementTree as ET


def thread_name(text):
    # "Main Thread (0xc768ab) (qtcraft, pid: 61430)" -> "Main Thread"
    name = re.sub(r"\s*\(0x[0-9a-f]+\).*$", "", text or "")
    return name or "?"


def time_report(path, top):
    cache = {}
    samples = collections.Counter()
    self_time = collections.defaultdict(collections.Counter)
    total_time = collections.defaultdict(collections.Counter)

    def resolve(element):
        ref = element.get("ref")
        if ref is not None:
            return cache.get(ref, element)
        identifier = element.get("id")
        if identifier is not None:
            cache[identifier] = element
        return element

    for _, element in ET.iterparse(path, events=("end",)):
        # Register every element with an id so later refs resolve.
        if element.get("id") is not None:
            cache[element.get("id")] = element
        if element.tag != "row":
            continue
        thread = None
        state = None
        weight = 1
        frames = []
        for child in element:
            child = resolve(child)
            if child.tag == "thread":
                thread = thread_name(child.get("fmt"))
            elif child.tag == "thread-state":
                state = child.get("fmt")
            elif child.tag == "weight":
                weight = int(child.text or "1000000") / 1e6
            elif child.tag == "tagged-backtrace" or child.tag == "backtrace":
                backtrace = child if child.tag == "backtrace" else child.find("backtrace")
                if backtrace is not None:
                    backtrace = resolve(backtrace)
                    for frame in backtrace.findall("frame"):
                        frame = resolve(frame)
                        frames.append(frame.get("name") or "?")
        if state not in (None, "Running"):
            continue
        samples[thread] += weight
        if frames:
            self_time[thread][frames[0]] += weight
            for name in set(frames):
                total_time[thread][name] += weight
    for thread, ms in samples.most_common():
        if ms < 50:
            continue
        print(f"\n== {thread}: {ms:.0f} ms on CPU")
        print("   self ms  function")
        for name, value in self_time[thread].most_common(top):
            print(f"   {value:7.0f}  {name[:150]}")
        print("   total ms function (inclusive)")
        for name, value in total_time[thread].most_common(top):
            print(f"   {value:7.0f}  {name[:150]}")


def qml_report(path, top):
    root = ET.parse(path).getroot()
    names = {}
    for event in root.iter("event"):
        index = event.get("index")
        kind = event.findtext("type") or ""
        location = event.findtext("filename") or ""
        line = event.findtext("line") or ""
        details = (event.findtext("details") or "").strip().replace("\n", " ")
        names[index] = (kind, f"{location.rsplit('/', 1)[-1]}:{line}", details[:80])
    totals = collections.Counter()
    counts = collections.Counter()
    for timeline in root.iter("range"):
        index = timeline.get("eventIndex")
        duration = int(timeline.get("duration") or 0)
        if index is None or duration == 0:
            continue
        totals[index] += duration
        counts[index] += 1
    by_kind = collections.Counter()
    for index, ns in totals.items():
        by_kind[names.get(index, ("?",))[0]] += ns
    print("== time by kind (ms)")
    for kind, ns in by_kind.most_common():
        print(f"   {ns / 1e6:9.1f}  {kind}")
    print(f"== top {top} events: total ms, calls, kind, location, details")
    for index, ns in totals.most_common(top):
        kind, location, details = names.get(index, ("?", "?", ""))
        print(f"   {ns / 1e6:9.2f} {counts[index]:7d}  {kind:12s} {location:32s} {details}")


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("time", "qml"):
        print(__doc__)
        return 2
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
    if sys.argv[1] == "time":
        time_report(sys.argv[2], top)
    else:
        qml_report(sys.argv[2], top)
    return 0


if __name__ == "__main__":
    sys.exit(main())
