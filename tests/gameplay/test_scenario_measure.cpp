// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcServerTestFixture.h"

#include "common/core/QcProcessMemory.h"
#include "common/core/QcTickPhase.h"
#include "QcTestUtil.h"
#include "server/blockentities/QcBlockEntityChecks.h"
#include "server/entities/QcEntityChecks.h"
#include "server/tick/QcTickStats.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <optional>
#include <vector>

using namespace Qt::StringLiterals;

// The server side of a benchmark scenario (scripts/bench-headless.sh, decision E-388) without a
// window and without waiting for real time: a server and the player of a raw client in one process,
// the scenario's chat lines sent as that player (an operator, so "~" works), and the server ticked as
// fast as it can. Ticks 0 to 59 are the warm-up of the rendered benchmark, then QTCRAFT_SCENARIO_SECONDS
// of game time are measured; a line "@<ms> <text>" is sent before tick <ms> / 50, a line without "@"
// before the first tick. A /tick sprint runs its ticks at once (they count, as in the rendered
// benchmark). Chunks are read, generated and written in the background as in the game; while some are
// under way the background gets a few milliseconds before a tick, so a flight does not outrun them.
// The world is saved in the background every 60 s of game time, like the autosave.
//
// Environment (scripts/bench-headless.sh sets it from scripts/bench-common.sh):
//   QTCRAFT_SCENARIO_MEASURE  file with the chat lines; unset: a short built-in scene (every test run)
//   QTCRAFT_SCENARIO_SECONDS  measured game seconds (default 20)
//   QTCRAFT_SCENARIO_WORLD    terrain, flat, void or showcase (default terrain); _LAYERS the options
//                             of a flat world
//   QTCRAFT_SCENARIO_SPAWN    "x,z" spawn column; _LOOK "yaw,pitch" (default 45,-10); _VIEW view
//                             distance (default 8); _SEED (default 42)
//   QTCRAFT_SCENARIO_SAMPLE   game seconds between SAMPLE lines (memory, chunks, creatures; 0: none)
// Prints one line "BENCH key=value ..." with the keys of the rendered benchmark that the server
// gives (tick_avg_ms, tick_worst_ms, tick_<phase>_avg_ms and _max_ms, server_mobs, ...) plus
// tick_p99_ms, loaded_chunks, max_loaded_chunks, rss_mb and peak_rss_mb (the whole process: server,
// the raw client and the test).
class TestScenarioMeasure : public QcServerTestFixture
{
    Q_OBJECT

public:
    TestScenarioMeasure()
    {
        m_unloadChunks = true;
        m_origin = QcServer::ClientOrigin::Local;
    }

private:
    struct Line
    {
        int tick = 0;
        QString text;
    };

    struct Scene
    {
        QList<Line> lines;
        int seconds = 20;
        QString world = u"terrain"_s;
        QString layers;
        std::optional<std::array<int, 2>> spawn;
        float yaw = 45.0f;
        float pitch = -10.0f;
        int view = 8;
        qint64 seed = 42;
        int sampleSeconds = 0;
    };

    static constexpr int warmupTicks = 60;
    static constexpr int autosaveTicks = 1200;
    static constexpr unsigned long pacingMs = 3;

    static QString env(const char *name, const QString &fallback = {})
    {
        const QString value = qEnvironmentVariable(name);
        return value.isEmpty() ? fallback : value;
    }

    static std::optional<std::array<int, 2>> pair(const QString &text)
    {
        const QStringList parts = text.split(u',');
        bool okA = false;
        bool okB = false;
        if (parts.size() != 2) {
            return std::nullopt;
        }
        const std::array<int, 2> values{parts[0].toInt(&okA), parts[1].toInt(&okB)};
        return okA && okB ? std::optional(values) : std::nullopt;
    }

    // "@<ms> text" before tick ms / 50, other lines before the first tick; empty lines are skipped.
    static QList<Line> parseLines(const QStringList &raw)
    {
        QList<Line> lines;
        for (const QString &text : raw) {
            if (text.trimmed().isEmpty()) {
                continue;
            }
            if (text.startsWith(u'@')) {
                const qsizetype blank = text.indexOf(u' ');
                lines.append({QStringView(text).mid(1, blank - 1).toInt() / 50, text.mid(blank + 1)});
            } else {
                lines.append({0, text});
            }
        }
        std::ranges::stable_sort(lines, {}, &Line::tick);
        return lines;
    }

    // Empty when the file of chat lines cannot be read.
    static std::optional<Scene> sceneFromEnvironment(const QString &file)
    {
        Scene scene;
        QFile input(file);
        if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return std::nullopt;
        }
        scene.lines = parseLines(QString::fromUtf8(input.readAll()).split(u'\n'));
        scene.seconds = env("QTCRAFT_SCENARIO_SECONDS", u"20"_s).toInt();
        scene.world = env("QTCRAFT_SCENARIO_WORLD", u"terrain"_s);
        scene.layers = env("QTCRAFT_SCENARIO_LAYERS");
        scene.spawn = pair(env("QTCRAFT_SCENARIO_SPAWN"));
        const std::optional<std::array<int, 2>> look = pair(env("QTCRAFT_SCENARIO_LOOK", u"45,-10"_s));
        scene.yaw = look ? static_cast<float>((*look)[0]) : scene.yaw;
        scene.pitch = look ? static_cast<float>((*look)[1]) : scene.pitch;
        scene.view = env("QTCRAFT_SCENARIO_VIEW", u"8"_s).toInt();
        scene.seed = env("QTCRAFT_SCENARIO_SEED", u"42"_s).toLongLong();
        scene.sampleSeconds = env("QTCRAFT_SCENARIO_SAMPLE", u"0"_s).toInt();
        return scene;
    }

    // Small enough for every test run: a flat world, a few creatures and items, a fill during the
    // measured part and a sprint of 40 ticks.
    static Scene builtinScene()
    {
        Scene scene;
        scene.lines = parseLines({u"/time set day"_s, u"/summon qtcraft:pig 4"_s, u"/spawnitem qtcraft:stick 1"_s,
                                  u"@3000 /fill ~2 ~ ~2 ~4 ~2 ~4 stone"_s, u"@3500 /tick sprint 40"_s});
        scene.seconds = 2;
        scene.world = u"flat"_s;
        scene.view = 2;
        scene.sampleSeconds = 1;
        return scene;
    }

    // Starts the server of `scene` in `directory` and lets the raw client's player join.
    void open(const Scene &scene, const QString &directory)
    {
        QcServerConfig config;
        config.worldDirectory = directory;
        config.autosaveSeconds = 0;
        config.seed = scene.seed;
        config.generator = scene.world;
        config.generatorOptions = scene.layers;
        config.spawnColumn = scene.spawn;
        config.viewDistance = scene.view;
        config.operators = {u"Raw"_s};
        std::unique_ptr<QcWorldGenerator> generator = QcWorldGenerators::create(scene.world, m_content, scene.seed, scene.layers, QcGeneratorVersion::current);
        QVERIFY2(generator, qPrintable(scene.world));
        m_server = std::make_unique<QcServer>(config, m_content, std::move(generator));
        QVERIFY(m_server->openWorld().has_value());

        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), m_origin);
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->hello(u"Raw"_s);
        QcTestUtil::pump();
    }

    // The player stands still and looks where the scene says; sent before every tick as the game's
    // client does.
    void stand(const Scene &scene)
    {
        QcPlayerInput input;
        input.sequence = ++m_sequence;
        input.yaw = scene.yaw;
        input.pitch = scene.pitch;
        m_client->connection.send(QcMsg::PlayerInput{input});
        QcTestUtil::pump();
    }

    // Ticks without measuring until the chunks around the player are there (at most 60 s of real time).
    void preload(const Scene &scene)
    {
        QElapsedTimer wall;
        wall.start();
        for (int i = 0; i < 20 || m_server->world().pendingGenerationCount() > 0; ++i) {
            if (wall.elapsed() > 60'000) {
                qWarning("Chunks still under way after 60 s; measuring anyway");
                break;
            }
            if (m_server->world().pendingGenerationCount() > 0) {
                QThread::msleep(pacingMs);
            }
            stand(scene);
            m_server->tick();
            QcTestUtil::pump();
        }
    }

    // One timed tick; messages are handled outside the time (they count in the phase "messages").
    // Ticks of a sprint (`onTimeline` false) are measured but do not move the scene's timeline: in
    // the rendered benchmark they run within milliseconds of real time.
    void timedTick(const Scene &scene, bool onTimeline = true)
    {
        if (m_server->world().pendingGenerationCount() > 0) {
            QThread::msleep(pacingMs);
        }
        stand(scene);
        QElapsedTimer timer;
        timer.start();
        m_server->tick();
        const qint64 ns = timer.nsecsElapsed();
        QcTestUtil::pump();
        if (m_tick >= warmupTicks) {
            m_stats.record(0, ns);
            m_stats.addPhases(m_server->tickPhases().lastTick());
            m_durations.push_back(ns);
        }
        m_mostLoaded = std::max(m_mostLoaded, m_server->world().loadedChunkCount());
        if (!onTimeline) {
            return;
        }
        ++m_tick;
        if (m_tick % autosaveTicks == 0) {
            m_server->saveInBackground();
        }
        if (scene.sampleSeconds > 0 && m_tick % (scene.sampleSeconds * 20) == 0) {
            sample();
        }
    }

    // A /tick sprint the last line started runs now, its ticks counted like the others.
    void runSprint(const Scene &scene)
    {
        const quint64 sprint = m_server->tickScheduler().sprintRemaining();
        if (sprint == 0) {
            return;
        }
        m_server->tickScheduler().stopSprint();
        for (quint64 i = 0; i < sprint; ++i) {
            timedTick(scene, false);
        }
    }

    void sample() const
    {
        qInfo("SAMPLE seconds=%.1f rss_mb=%.1f loaded_chunks=%lld mobs=%lld items=%lld falling=%lld", static_cast<double>(m_tick) / 20.0,
              QcProcessMemory::currentMb(), static_cast<qlonglong>(m_server->world().loadedChunkCount()),
              static_cast<qlonglong>(m_server->mobs().mobs().size()),
              static_cast<qlonglong>(m_server->items().items().size()), static_cast<qlonglong>(m_server->fallingBlocks().all().size()));
    }

    static QString phasePairs(const QcTickStats::Summary &ticks)
    {
        QStringList pairs;
        for (int phase = 0; phase < QcTickPhases::count; ++phase) {
            const QLatin1StringView id(QcTickPhases::ids[static_cast<std::size_t>(phase)]);
            pairs.append(u"tick_%1_avg_ms=%2"_s.arg(id).arg(ticks.phaseAverageMs[static_cast<std::size_t>(phase)], 0, 'f', 4));
            pairs.append(u"tick_%1_max_ms=%2"_s.arg(id).arg(ticks.phaseWorstMs[static_cast<std::size_t>(phase)], 0, 'f', 3));
        }
        return pairs.join(u' ');
    }

    QString report(double wallSeconds)
    {
        const QcTickStats::Summary ticks = m_stats.summary();
        std::vector<qint64> sorted = m_durations;
        std::ranges::sort(sorted, std::greater{});
        const double p99 = sorted.empty() ? 0.0 : static_cast<double>(sorted.at(sorted.size() / 100)) / 1e6;
        const QcBlockEntities &blockEntities = m_server->blockEntities();
        const qsizetype blockEntityCount = blockEntities.containerCount() + blockEntities.machineCount() + blockEntities.saplingCount();
        return u"BENCH tick_avg_ms=%1 tick_p99_ms=%2 tick_worst_ms=%3 ticks=%4 wall_s=%5 rss_mb=%6 peak_rss_mb=%7 "
               u"loaded_chunks=%8 max_loaded_chunks=%9 server_mobs=%10 server_items=%11 server_falling_blocks=%12 "
               u"server_block_entities=%13 block_entity_problems=%14 entity_problems=%15 %16"_s.arg(ticks.averageMs, 0, 'f', 3)
            .arg(p99, 0, 'f', 3)
            .arg(ticks.worstMs, 0, 'f', 3)
            .arg(ticks.ticks)
            .arg(wallSeconds, 0, 'f', 2)
            .arg(QcProcessMemory::currentMb(), 0, 'f', 1)
            .arg(QcProcessMemory::peakMb(), 0, 'f', 1)
            .arg(m_server->world().loadedChunkCount())
            .arg(m_mostLoaded)
            .arg(m_server->mobs().mobs().size())
            .arg(m_server->items().items().size())
            .arg(m_server->fallingBlocks().all().size())
            .arg(blockEntityCount)
            .arg(QcBlockEntityChecks::problems(m_server->world()).size())
            .arg(QcEntityChecks::problems(m_server->entities()).size())
            .arg(phasePairs(ticks));
    }

    // Runs the scene and returns its BENCH line.
    QString run(const Scene &scene)
    {
        QTemporaryDir directory;
        open(scene, directory.path());
        if (!m_server) {
            return {};
        }
        preload(scene);
        QElapsedTimer wall;
        wall.start();
        const int total = warmupTicks + scene.seconds * 20;
        qsizetype next = 0;
        while (m_tick < total) {
            while (next < scene.lines.size() && scene.lines.at(next).tick <= m_tick) {
                chat(scene.lines.at(next++).text);
                runSprint(scene);
            }
            timedTick(scene);
        }
        m_server->waitForStorage();
        const QString line = report(static_cast<double>(wall.elapsed()) / 1e3);
        m_client.reset();
        m_server.reset();
        return line;
    }

private slots:
    void scenario()
    {
        const QString file = qEnvironmentVariable("QTCRAFT_SCENARIO_MEASURE");
        const std::optional<Scene> read = file.isEmpty() ? builtinScene() : sceneFromEnvironment(file);
        QVERIFY2(read, qPrintable(u"Cannot read the chat lines %1"_s.arg(file)));
        const Scene &scene = *read;
        const QString line = run(scene);
        QVERIFY(!line.isEmpty());
        qInfo().noquote() << line;

        // Every measured tick counts, the sprint's included; the scene's creatures and items exist.
        if (file.isEmpty()) {
            QCOMPARE(m_stats.summary().ticks, quint64(scene.seconds * 20 + 40));
            QVERIFY2(line.contains(u" server_mobs="_s) && !line.contains(u" server_mobs=0 "_s), qPrintable(line));
            QVERIFY2(line.contains(u" tick_mobs_avg_ms="_s), qPrintable(line));
            QVERIFY2(line.contains(u" entity_problems=0 "_s), qPrintable(line));
        }
    }

private:
    QcTickStats m_stats;
    std::vector<qint64> m_durations;
    int m_tick = 0;
    qsizetype m_mostLoaded = 0;
};

QTEST_GUILESS_MAIN(TestScenarioMeasure)
#include "test_scenario_measure.moc"
