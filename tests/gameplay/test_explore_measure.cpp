#include "QcServerTestFixture.h"

#include "common/core/QcTickPhase.h"
#include "QcTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QElapsedTimer>
#include <QThread>
#include <QTemporaryDir>

#include <algorithm>
#include <vector>

using namespace Qt::StringLiterals;

// The exploration flight of the benchmarks (scenario "explore", stage 31.3) without a window and
// without waiting for real time: on the terrain of seed 42 the player is teleported 5 m east at y 200
// every 5 ticks and a 3 x 3 glass platform is put at y 70 below him every 20 ticks, so every chunk
// passed is changed; a save runs in the background every 1200 ticks (the autosave counts real time,
// which this flight does not take). The server ticks as fast as it can while chunks are read,
// generated and written in the background as in the game (they get a few milliseconds before a tick
// while chunks are under way, so the flight does not outrun them), so a tick that waits for the disk
// shows.
// Small in every test run (checks that the server keeps up with the flight); QTCRAFT_EXPLORE_MEASURE=
// <seconds of game time> (300 like the benchmark) measures at view distance 8 and prints MEASURE
// lines: tick mean, p99 and maximum, the phases of the slowest ticks and the loaded chunks (Release).
class TestExploreMeasure : public QcServerTestFixture
{
    Q_OBJECT

public:
    TestExploreMeasure() { m_unloadChunks = true; }

private:
    struct Tick
    {
        qint64 ns = 0;
        int index = 0;
        QcTickPhaseTimer::Durations phases{};
    };

    void console(const QString &line)
    {
        m_server->runConsoleCommand(line, [](const QcMsg::ChatMessage &) {});
    }

    // One tick of the server, timed; the client's messages are handled after it, outside the time.
    Tick timedTick(int index)
    {
        QElapsedTimer timer;
        timer.start();
        m_server->tick();
        Tick tick{timer.nsecsElapsed(), index, m_server->tickPhases().lastTick()};
        QcTestUtil::pump();
        return tick;
    }

    // Real time the background gets before a tick while chunks are under way.
    static constexpr unsigned long pacingMs = 3;

    static void report(const char *what, double value, const char *unit) { qInfo("MEASURE %s %.3f %s", what, value, unit); }

    static QString phasesOf(const Tick &tick)
    {
        QStringList parts;
        for (int phase = 0; phase < QcTickPhases::count; ++phase) {
            if (tick.phases.at(static_cast<std::size_t>(phase)) >= 500'000) {
                parts.append(u"%1=%2"_s.arg(QString::fromLatin1(QcTickPhases::ids.at(static_cast<std::size_t>(phase))))
                                .arg(static_cast<double>(tick.phases.at(static_cast<std::size_t>(phase))) / 1e6, 0, 'f', 2));
            }
        }
        return parts.join(u' ');
    }

    void reportTicks(std::vector<Tick> ticks)
    {
        qint64 total = 0;
        for (const Tick &tick : ticks) {
            total += tick.ns;
        }
        report("ticks", static_cast<double>(ticks.size()), "");
        report("tick-mean", static_cast<double>(total) / static_cast<double>(ticks.size()) / 1e6, "ms");
        std::ranges::sort(ticks, [](const Tick &a, const Tick &b) { return a.ns > b.ns; });
        const std::size_t slowestPercent = ticks.size() / 100;
        report("tick-p99", static_cast<double>(ticks.at(slowestPercent).ns) / 1e6, "ms");
        report("tick-max", static_cast<double>(ticks.front().ns) / 1e6, "ms");
        for (std::size_t i = 0; i < std::min<std::size_t>(5, ticks.size()); ++i) {
            qInfo("MEASURE slow-tick %d %.3f ms %s", ticks.at(i).index, static_cast<double>(ticks.at(i).ns) / 1e6, qPrintable(phasesOf(ticks.at(i))));
        }
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void flight()
    {
        const int seconds = qEnvironmentVariableIntValue("QTCRAFT_EXPLORE_MEASURE");
        const bool full = seconds > 0;
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        m_viewDistance = full ? 8 : 3;
        join(config, std::make_unique<QcTerrainGenerator>(m_content, 42));
        console(u"gamemode creative Raw"_s);
        console(u"time set day"_s);
        const QcBlockPos start = m_feet;

        QElapsedTimer wall;
        wall.start();
        std::vector<Tick> ticks;
        const int steps = (full ? seconds : 20) * 4;
        qsizetype mostLoaded = 0;
        for (int step = 0; step < steps; ++step) {
            const int x = start.x + 5 * (step + 1);
            if (step % 4 == 0) {
                console(u"fill %1 70 %2 %3 70 %4 glass"_s.arg(x - 6).arg(start.z - 1).arg(x - 4).arg(start.z + 1));
            }
            console(u"tp Raw %1 200 %2"_s.arg(x).arg(start.z));
            for (int i = 0; i < 5; ++i) {
                const int index = static_cast<int>(ticks.size());
                if (index > 0 && index % 1200 == 0) {
                    m_server->saveInBackground();
                }
                // The background gets a little time while chunks are under way, as between the ticks of
                // the game (50 ms); without it the flight would outrun every chunk.
                if (m_server->world().pendingGenerationCount() > 0) {
                    QThread::msleep(pacingMs);
                }
                ticks.push_back(timedTick(index));
            }
            mostLoaded = std::max(mostLoaded, m_server->world().loadedChunkCount());
        }
        m_server->waitForStorage();

        // The server keeps up: the chunks around the player are there at the end of the flight.
        for (int i = 0; i < 400 && !m_server->world().chunk(QcChunkStreaming::chunkOf(m_client->motion.position)); ++i) {
            ticks.push_back(timedTick(static_cast<int>(ticks.size())));
        }
        QVERIFY(m_server->world().chunk(QcChunkStreaming::chunkOf(m_client->motion.position)));
        const qsizetype square = static_cast<qsizetype>(2 * m_viewDistance + 3) * static_cast<qsizetype>(2 * m_viewDistance + 3);
        QVERIFY2(mostLoaded <= 2 * square, qPrintable(QString::number(mostLoaded)));

        report("wall", static_cast<double>(wall.elapsed()) / 1e3, "s");
        report("most-loaded", static_cast<double>(mostLoaded), "chunks");
        report("stored", static_cast<double>(m_server->world().saves().storedChunkCount()), "chunks");
        reportTicks(std::move(ticks));
        cleanup();
    }
};

QTEST_GUILESS_MAIN(TestExploreMeasure)
#include "test_explore_measure.moc"
