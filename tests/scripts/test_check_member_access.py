#!/usr/bin/env python3
"""Checks scripts/check-member-access.py (rule "Getter und Setter für mehrfach benutzte Member", E-891) on
in-memory samples: counted places, skipped constructors and accessors, plain data structs, opt-out markers.
Reads no project file. Usage: test_check_member_access.py <path of check-member-access.py>
"""

import importlib.util
import sys


def load_module(path):
    spec = importlib.util.spec_from_file_location("check_member_access", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


HEADER = """
#include <QMutex>
class QcSample
{
public:
    explicit QcSample(int start);
    ~QcSample();
    int count() const { return m_count; }
    void setCount(int count) { m_count = count; }
    void step();
    void open();
    bool same(const QcSample &other) const { return other.m_total == m_total; }

private:
    int m_count = 0;
    int m_total = 0;            // read in three methods, no getter
    QList<int> m_items;         // written four times, no setter
    bool m_open = false;
    int m_quiet = 0;            // NOLINT(qc-member-access): sample exemption
    int m_unreasoned = 0;       // NOLINT(qc-member-access):
    QMutex m_mutex;
    int m_once = 0;             // set in the constructor, read twice
};
"""

SOURCE = """
#include "QcSample.h"
QcSample::QcSample(int start)
    : m_count(start)
    , m_total{start}
{
    m_items.append(start);
    m_once = m_total + m_count;
}

QcSample::~QcSample()
{
    m_items.clear();
}

void QcSample::step()
{
    QMutexLocker lock(&m_mutex);
    ++m_count;
    m_items.append(m_total);
    m_items.removeFirst();
    m_items << m_once;
    m_quiet = m_quiet + m_quiet + m_quiet;
    m_unreasoned = m_total + m_once;
}

void QcSample::open()
{
    m_open = true;
    m_items[0] = m_total;
}
"""

PLAIN = """
struct QcPlain
{
    int m_a = 0;
    int m_b = 0;
    int sum() const { return m_a + m_b + m_a + m_b; }
    bool operator==(const QcPlain &other) const = default;
};
"""


def run(module):
    sources = {"src/x/QcSample.h": HEADER, "src/x/QcSample.cpp": SOURCE, "src/x/QcPlain.h": PLAIN}
    found = {finding[4]: finding for finding in module.analyse(sources)}
    expected = {
        "m_count": ("bypass", 0, 1, ""),           # one direct write besides the accessors: 1 place only
        "m_total": ("missing", 4, 0, "getter"),
        "m_items": ("missing", 0, 4, "setter"),
        "m_unreasoned": ("unreasoned", 0, 0, ""),
    }
    failures = []
    for member, (kind, reads, writes, missing) in expected.items():
        finding = found.get(member)
        if member == "m_count":
            if finding is not None:
                failures.append(f"{member}: one place outside the accessors must not be listed, got {finding}")
            continue
        if finding is None:
            failures.append(f"{member}: expected {kind}, nothing listed")
            continue
        got = (finding[0], finding[5], finding[6], finding[7])
        if got != (kind, reads, writes, missing):
            failures.append(f"{member}: expected {(kind, reads, writes, missing)}, got {got}")
    for member in ("m_open", "m_quiet", "m_mutex", "m_once", "m_a", "m_b"):
        if member in found:
            failures.append(f"{member}: must not be listed, got {found[member]}")
    if found.get("m_total") and found["m_total"][2] != 16:
        failures.append(f"m_total: declaration line 16 expected, got {found['m_total'][2]}")

    selected = module.analyse(sources, {"src/x/Other.cpp"})
    if selected:
        failures.append(f"a selection without the class's files must list nothing, got {selected}")
    if not module.analyse(sources, {"src/x/QcSample.cpp"}):
        failures.append("a selection with the class's source file must list its findings")

    bypass = {"src/y/QcB.h": """
class QcB
{
public:
    int value() const { return m_value; }
    void setValue(int value) { m_value = value; }
    int twice() const { return m_value + m_value; }
    void reset() { m_value = 0; }
private:
    int m_value = 0;
};
"""}
    links = {"src/z/QcL.h": """
class QcL
{
public:
    QcL(QcWorld &world, QcWorld *view, QcWorld *fixed, QcWorld *moving)
        : m_world(world), m_constWorld(world), m_view(view), m_fixed(fixed), m_moving(moving) {}
    void a() { m_world.tick(); m_constWorld.size(); m_view->tick(); m_fixed->tick(); m_moving->tick(); }
    void b() { m_world.tick(); m_constWorld.size(); m_view->tick(); m_fixed->tick(); m_moving->tick(); }
    void c() { m_world.tick(); m_constWorld.size(); m_view->tick(); m_fixed->tick(); m_moving = nullptr; }
private:
    QcWorld &m_world;
    const QcWorld &m_constWorld;
    QcWorld *const m_view;
    QcWorld *m_fixed = nullptr;
    QcWorld *m_moving = nullptr;
    std::unique_ptr<QcWorld> m_owned;
    void d() { m_owned->tick(); m_owned->tick(); m_owned->tick(); }
};
"""}
    found = [(item[4], item[5], item[6]) for item in module.analyse(links)]
    if found != [("m_moving", 2, 1), ("m_owned", 3, 0)]:
        failures.append(f"QcL: only the reassigned pointer and the smart pointer may be listed, got {found}")

    shapes = {"src/t/QcShapes.h": """
template <typename T>
class QcBox
{
public:
    void put(T value);
    T first() const { return m_values.front() + m_values.back(); }
private:
    QList<T> m_values;
};

template <typename T>
void QcBox<T>::put(T value)
{
    m_values.append(value);
}

class QcOuter
{
public:
    struct Inner
    {
        void bump() { ++m_hits; ++m_hits; }
        int last() const { return m_hits + 1; }
        int m_hits = 0;
    };
    void run()
    {
        const auto each = [this](int step) { m_timer.start(step); return m_timer.isActive(); };
        each(1);
        m_timer.stop();
    }
private:
    QTimer m_timer;
};
"""}
    found = sorted((item[3], item[4], item[5], item[6], item[7]) for item in module.analyse(shapes))
    expected = [("QcBox", "m_values", 2, 1, "getter/setter"), ("QcOuter", "m_timer", 1, 2, "getter/setter"),
                ("QcOuter::Inner", "m_hits", 1, 2, "getter/setter")]
    if found != expected:
        failures.append(f"template, nested struct and lambda: expected {expected}, got {found}")

    found = module.analyse(bypass)
    if [(item[0], item[4], item[5], item[6]) for item in found] != [("bypass", "m_value", 2, 1)]:
        failures.append(f"QcB: expected one bypass of m_value (2 reads, 1 write), got {found}")
    return failures


def main(arguments):
    module = load_module(arguments[0])
    failures = run(module)
    for failure in failures:
        print(f"FAIL: {failure}")
    print("check-member-access test: " + ("failed" if failures else "all samples judged correctly"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
