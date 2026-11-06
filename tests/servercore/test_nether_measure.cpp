// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcTickPhase.h"
#include "fixtures/QcTestUtil.h"
#include "server/blocks/QcLiquidFlow.h"
#include "server/core/QcServer.h"
#include "server/core/QcServerState.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <vector>

using namespace Qt::StringLiterals;

// What the lava of the nether costs the server (open question 6 of docs/Plan-Nether-Brauen.md,
// decision E-1035), ticked by hand as fast as the server can: the nether's chunks around the origin
// are loaded with their springs running, and the time of each tick, of its liquid phase, the steps
// and the changed blocks are written to the log. QC_MEASURE_RADIUS (chunks, default 8) and
// QC_MEASURE_TICKS (default 1200, a minute of game time) set the size; as a test it runs a short
namespace {

// What one body of lava looks like: the springs in it and the most falling and sheet cells at a height.
struct Fall
{
    int springs = 0;
    int width = 0;
    int sheet = 0;
};

// How many of `springs` in `world` open onto a shelf: free beside them, solid below that.
int shelvesOf(const QcServerWorld &world, const QcContentRegistry &content, const QList<QcBlockPos> &springs)
{
    const auto solid = [&](QcBlockPos pos) {
        const std::optional<QcBlockId> block = world.blockAt(pos);
        return block && content.block(*block).solid;
    };
    int shelves = 0;
    for (const QcBlockPos &spring : springs) {
        for (const QcBlockPos side : {spring.offset(1, 0, 0), spring.offset(-1, 0, 0), spring.offset(0, 0, 1), spring.offset(0, 0, -1)}) {
            shelves += !solid(side) && solid(side.offset(0, -1, 0)) ? 1 : 0;
        }
    }
    return shelves;
}

// The connected bodies of lava above the sea in a square of chunks.
class LavaBodies
{
public:
    LavaBodies(const QcServerWorld &world, const QcContentRegistry &content, QcChunkPos corner, int chunks)
        : m_world(world)
        , m_content(content)
    {
        const QcBlockId lava = *content.blockId(u"qtcraft:lava"_s);
        for (int y = 34; y < 127; ++y) {
            for (int z = corner.z * 16; z < (corner.z + chunks) * 16; ++z) {
                for (int x = corner.x * 16; x < (corner.x + chunks) * 16; ++x) {
                    const std::optional<QcBlockId> block = world.blockAt({x, y, z});
                    if (block && content.isSameBlock(*block, lava)) {
                        m_cells.insert({x, y, z});
                    }
                }
            }
        }
    }

    // The next body, empty when none is left.
    QList<QcBlockPos> next()
    {
        if (m_cells.isEmpty()) {
            return {};
        }
        QList<QcBlockPos> body{*m_cells.begin()};
        m_cells.erase(m_cells.begin());
        for (qsizetype i = 0; i < body.size(); ++i) {
            const QcBlockPos at = body[i];
            for (const QcBlockPos step : {at.offset(1, 0, 0), at.offset(-1, 0, 0), at.offset(0, 1, 0), at.offset(0, -1, 0), at.offset(0, 0, 1), at.offset(0, 0, -1)}) {
                if (m_cells.remove(step)) {
                    body.append(step);
                }
            }
        }
        return body;
    }

    Fall fallOf(const QList<QcBlockPos> &body, const QList<QcBlockPos> &springs) const
    {
        QHash<int, int> falling;
        QHash<int, int> sheet;
        for (const QcBlockPos &cell : body) {
            ++(QcLiquidFlow::isFalling(m_content, *m_world.blockAt(cell)) ? falling : sheet)[cell.y];
        }
        const auto most = [](const QHash<int, int> &counts) {
            return counts.isEmpty() ? 0 : *std::ranges::max_element(counts);
        };
        const auto inBody = [&body](QcBlockPos spring) { return body.contains(spring); };
        return {static_cast<int>(std::ranges::count_if(springs, inBody)), most(falling), most(sheet)};
    }

private:
    const QcServerWorld &m_world;
    const QcContentRegistry &m_content;
    QSet<QcBlockPos> m_cells;
};

} // namespace

// form and only checks that the plan of steps is not left full.
class TestNetherMeasure : public QObject
{
    Q_OBJECT

private slots:
    void lavaCost()
    {
        const int radius = qEnvironmentVariableIsSet("QC_MEASURE_RADIUS") ? qEnvironmentVariableIntValue("QC_MEASURE_RADIUS") : 3;
        const int ticks = qEnvironmentVariableIsSet("QC_MEASURE_TICKS") ? qEnvironmentVariableIntValue("QC_MEASURE_TICKS") : 200;
        const std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldName = u"Measure"_s;
        config.seed = 42;
        config.worldDirectory = dir.path();
        config.keepChunksLoaded = true;
        QcServer server(config, content, std::make_unique<QcTestTerrainGenerator>(content));
        QVERIFY(server.openWorld().has_value());
        const QcResult<QcServerDimension *> opened = server.openDimension(u"qtcraft:nether"_s);
        QVERIFY(opened.has_value());

        QElapsedTimer loading;
        loading.start();
        for (int z = -radius; z <= radius; ++z) {
            for (int x = -radius; x <= radius; ++x) {
                (*opened)->world().loadChunk({x, z});
            }
        }
        const int chunks = (2 * radius + 1) * (2 * radius + 1);
        qInfo("loaded %d chunks in %.1f ms (%.2f ms each)", chunks, static_cast<double>(loading.nsecsElapsed()) / 1.0e6, static_cast<double>(loading.nsecsElapsed()) / 1.0e6 / chunks);

        std::vector<double> times;
        qint64 steps = 0;
        qsizetype peakPlan = 0;
        for (int tick = 0; tick < ticks; ++tick) {
            QElapsedTimer timer;
            timer.start();
            QcTestUtil::ticks(server, 1);
            times.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
            peakPlan = std::max(peakPlan, (*opened)->liquids().plannedCount());
            steps += (*opened)->liquids().plannedCount();
            if (tick % 200 == 199) {
                qInfo("tick %d: planned %lld, dropped %llu", tick + 1, static_cast<long long>((*opened)->liquids().plannedCount()),
                      static_cast<unsigned long long>((*opened)->liquids().droppedCount()));
            }
        }
        std::ranges::sort(times);
        double sum = 0.0;
        for (const double time : times) {
            sum += time;
        }
        qInfo("NETHER radius=%d ticks=%d tick_avg_ms=%.3f tick_p99_ms=%.3f tick_max_ms=%.3f planned_peak=%lld planned_ticks_sum=%lld dropped=%llu", radius, ticks,
              sum / static_cast<double>(times.size()), times[times.size() * 99 / 100], times.back(), static_cast<long long>(peakPlan), static_cast<long long>(steps),
              static_cast<unsigned long long>((*opened)->liquids().droppedCount()));
        QCOMPARE((*opened)->liquids().droppedCount(), quint64{0});
    }

    // What the springs around a place make once they ran: a fall is a connected body of lava above
    // the sea, thin and from one spring, no curtain (E-1039).
    void fallsOfTheSprings()
    {
        const int radius = qEnvironmentVariableIsSet("QC_MEASURE_RADIUS") ? qEnvironmentVariableIntValue("QC_MEASURE_RADIUS") : 6;
        const int ticks = qEnvironmentVariableIsSet("QC_MEASURE_TICKS") ? qEnvironmentVariableIntValue("QC_MEASURE_TICKS") : 1000;
        const std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldName = u"Falls"_s;
        config.seed = 42;
        config.worldDirectory = dir.path();
        config.keepChunksLoaded = true;
        QcServer server(config, content, std::make_unique<QcTestTerrainGenerator>(content));
        QVERIFY(server.openWorld().has_value());
        const QcResult<QcServerDimension *> opened = server.openDimension(u"qtcraft:nether"_s);
        QVERIFY(opened.has_value());
        QcServerWorld &world = (*opened)->world();

        // Around the ruined portal of seed 42.
        const QcChunkPos middle{-4, 11};
        QList<QcBlockPos> springs;
        for (int z = -radius; z <= radius; ++z) {
            for (int x = -radius; x <= radius; ++x) {
                const QcChunkPos pos{middle.x + x, middle.z + z};
                world.loadChunk(pos);
                springs.append(world.generator().liquidStartsIn(*world.chunk(pos)));
            }
        }
        const int shelves = shelvesOf(world, *content, springs);
        const int chunks = (2 * radius + 1) * (2 * radius + 1);
        qInfo("FALLS springs %lld in %d chunks (%.2f per chunk), %d open onto a shelf", static_cast<long long>(springs.size()), chunks,
              static_cast<double>(springs.size()) / chunks, shelves);

        QcTestUtil::ticks(server, ticks);

        const QcChunkPos corner{middle.x - radius, middle.z - radius};
        LavaBodies bodies(world, *content, corner, 2 * radius + 1);
        std::vector<int> widths;
        for (QList<QcBlockPos> body = bodies.next(); !body.isEmpty(); body = bodies.next()) {
            const Fall fall = bodies.fallOf(body, springs);
            qInfo("FALL cells %lld, springs %d, falling width %d, widest sheet %d", static_cast<long long>(body.size()), fall.springs, fall.width, fall.sheet);
            widths.push_back(fall.width);
        }
        std::ranges::sort(widths);
        qInfo("FALLS bodies %lld, median falling width %d, widest %d", static_cast<long long>(widths.size()),
              widths.empty() ? 0 : widths[widths.size() / 2], widths.empty() ? 0 : widths.back());
        QCOMPARE(shelves, 0);
        QVERIFY(!widths.empty() && widths.back() <= 3);
    }
};

QTEST_GUILESS_MAIN(TestNetherMeasure)
#include "test_nether_measure.moc"
