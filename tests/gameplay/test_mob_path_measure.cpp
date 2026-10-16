#include "QcBaseContent.h"
#include "QcDayCycle.h"
#include "QcFlatGenerator.h"
#include "QcMobManager.h"
#include "QcServerWorld.h"
#include "QcTestTerrainGenerator.h"

#if __has_include("QcPathPlanner.h")
#include "QcPathPlanner.h"
#define QC_HAS_PATHS 1
#endif

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

// Cost of the creature tick (the server's "mobs" phase) with 200 and 1000 zombies chasing a player
// who walks in a circle, on rolling hills with a lake, in a maze and with the player shut in a glass
// room in the maze (decision E-400). A measurement,
// not a check: set QTCRAFT_PATH_MEASURE=1 and use a Release build. The ticks run as fast as they
// can, not in real time. The file only uses what QcMobManager offered before path finding, so the
// same file measures the state before it.
class TestMobPathMeasure : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = [] {
        QStringList warnings;
        return *QcBaseContent::load(warnings);
    }();
    quint32 m_nextId = 100;

    static constexpr double midnight = QcDayCycle::dayLength * 0.75;
    static constexpr int radius = 3;

    std::unique_ptr<QcServerWorld> loaded(std::unique_ptr<QcWorldGenerator> generator) const
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::move(generator));
        for (int x = -radius; x <= radius; ++x) {
            for (int z = -radius; z <= radius; ++z) {
                world->loadChunk({x, z});
            }
        }
        return world;
    }

    // A maze on flat ground: rooms of 3 x 3 cells between walls three blocks high on every fourth
    // line, half of the walls of a room with a gap of one cell, where and which fixed by a hash; the
    // middle around the player's circle is open.
    std::unique_ptr<QcServerWorld> maze() const
    {
        auto world = loaded(std::make_unique<QcFlatGenerator>(m_content, u"bedrock,3*stone,grass_block"_s));
        const QcBlockId stone = *m_content->blockId(u"qtcraft:stone"_s);
        const int reach = radius * 16 + 15;
        for (int b = -reach / 4 * 4; b <= reach; b += 4) {
            for (int a = -reach; a <= reach; ++a) {
                const int offset = qFloorMod(a, 4);
                const quint32 hash = quint32((a - offset) * 73856093) ^ quint32(b * 19349663);
                // Every other wall piece (by the hash) has its gap: dead ends and closed pockets.
                const bool wallX = offset == 0 || offset != 1 + int(hash % 3) || (hash >> 4) % 2 == 0;
                const bool wallZ = offset == 0 || offset != 1 + int((hash >> 8) % 3) || (hash >> 12) % 2 == 0;
                for (int y = 5; y <= 7; ++y) {
                    if (wallX) {
                        world->setBlock({b, y, a}, stone);
                    }
                    if (wallZ) {
                        world->setBlock({a, y, b}, stone);
                    }
                }
            }
        }
        for (int y = 5; y <= 7; ++y) {
            for (int x = -3; x <= 8; ++x) {
                for (int z = -3; z <= 8; ++z) {
                    world->setBlock({x, y, z}, qcAirBlockId);
                }
            }
        }
        return world;
    }

    static int qFloorMod(int value, int modulus) { return ((value % modulus) + modulus) % modulus; }

    static double surfaceY(const QcServerWorld &world, int x, int z)
    {
        for (int y = 120; y > 0; --y) {
            const std::optional<QcBlockId> block = world.blockAt({x, y, z});
            if (block && *block != qcAirBlockId && world.content().block(*block).solid) {
                return y + 1.0;
            }
        }
        return 64.0;
    }

    void measure(const char *name, QcServerWorld &world, int count, qint64 nodesPerTick)
    {
        QcMobManager mobs(m_content, 7);
        mobs.setNaturalSpawning(false);
#ifdef QC_HAS_PATHS
        mobs.setPathLimits(nodesPerTick, 512, 64);
#else
        Q_UNUSED(nodesPerTick);
#endif
        const int zombie = *m_content->mobIndex(u"qtcraft:zombie"_s);
        for (int i = 0; i < count; ++i) {
            const double angle = i * 2.399963;
            const double distance = 8.0 + (i * 37 % 120) / 10.0;
            // Off the maze's wall lines.
            int x = int(std::floor(std::cos(angle) * distance)) + 2;
            int z = int(std::floor(std::sin(angle) * distance)) + 2;
            x += qFloorMod(x, 4) == 0 ? 1 : 0;
            z += qFloorMod(z, 4) == 0 ? 1 : 0;
            mobs.spawn(zombie, {x + 0.5, surfaceY(world, x, z), z + 0.5}, m_nextId++);
        }

        // The player walks a circle of 4 m around (2.5, 2.5) at 2 m/s.
        const auto player = [&world](int tick) {
            const double angle = tick * 0.025;
            const double x = 2.5 + 4.0 * std::cos(angle);
            const double z = 2.5 + 4.0 * std::sin(angle);
            return QList<QcMobManager::PlayerView>{{7, {x, surfaceY(world, int(std::floor(x)), int(std::floor(z))), z}, true}};
        };
        for (int tick = 0; tick < 20; ++tick) {
            mobs.tick(world, player(tick), midnight, 0.05, [this] { return m_nextId++; });
        }

        constexpr int ticks = 200;
        double total = 0.0;
        double worst = 0.0;
        qint64 nodes = 0;
        for (int tick = 20; tick < 20 + ticks; ++tick) {
            const QList<QcMobManager::PlayerView> players = player(tick);
            QElapsedTimer timer;
            timer.start();
            mobs.tick(world, players, midnight, 0.05, [this] { return m_nextId++; });
            const double ms = double(timer.nsecsElapsed()) / 1e6;
            total += ms;
            worst = std::max(worst, ms);
#ifdef QC_HAS_PATHS
            nodes += mobs.paths().nodesLastTick();
#endif
        }
        qsizetype chasing = 0;
        for (const QcCreature &mob : mobs.mobs()) {
            chasing += mob.chasing != 0 ? 1 : 0;
        }
        qint64 searches = 0;
#ifdef QC_HAS_PATHS
        searches = mobs.paths().searches();
#endif
        qInfo("MEASURE %s %d zombies, %lld nodes/tick budget: %.3f ms mean, %.3f ms max per tick, %.0f nodes/tick, %lld searches, "
              "%lld chasing at the end",
              name, count, qlonglong(nodesPerTick), total / ticks, worst, double(nodes) / ticks, qlonglong(searches), qlonglong(chasing));
    }

private slots:
    void initTestCase()
    {
        if (qEnvironmentVariableIntValue("QTCRAFT_PATH_MEASURE") != 1) {
            QSKIP("measurement: set QTCRAFT_PATH_MEASURE=1");
        }
    }

    void hills()
    {
        auto world = loaded(std::make_unique<QcTestTerrainGenerator>(m_content));
        for (const int count : {200, 1000}) {
            measure("hills", *world, count, 4096);
            measure("hills-straight", *world, count, 0);
        }
    }

    void labyrinth()
    {
        auto world = maze();
        for (const int count : {200, 1000}) {
            measure("maze", *world, count, 4096);
            measure("maze-straight", *world, count, 0);
        }
    }

    // The worst case: the player in a closed glass room in the maze, out of every creature's reach,
    // so every search ends at its node budget.
    void unreachable()
    {
        auto world = maze();
        const QcBlockId glass = *m_content->blockId(u"qtcraft:glass"_s);
        for (int x = -3; x <= 8; ++x) {
            for (int z = -3; z <= 8; ++z) {
                const bool wall = x == -3 || x == 8 || z == -3 || z == 8;
                for (int y = 5; y <= 8; ++y) {
                    if (wall || y == 8) {
                        world->setBlock({x, y, z}, glass);
                    }
                }
            }
        }
        for (const int count : {200, 1000}) {
            measure("closed", *world, count, 4096);
            measure("closed-straight", *world, count, 0);
        }
    }
};

QTEST_GUILESS_MAIN(TestMobPathMeasure)
#include "test_mob_path_measure.moc"
