// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcServerTestFixture.h"
#include "fixtures/QcTestUtil.h"
#include "server/mobbrains/QcFishSwimming.h"

#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

// Fish in narrow and shallow water (decision E-972): they pick their goals only where their
// body has room in the water, follow canals around corners, do not jitter against walls or the
// ground, never leave the water and idle in a hole too small to swim in. The server's random
// comes from the world seed: fixed seeds plus one random seed per run (decision E-846). Ticks are
// stepped by hand.
class TestFishPaths : public QcServerTestFixture
{
    Q_OBJECT

private:
    // What a fish did over some ticks.
    struct Watch
    {
        int ticks = 0;
        // Ticks with the centre out of the water or the body above the surface.
        int outside = 0;
        // Ticks after which a wall or the ground stopped it.
        int blocked = 0;
        // Reversals of its horizontal heading.
        int flips = 0;
        // Cells its feet were in.
        QSet<QcBlockPos> cells;
        QcVec3 low{1e9, 1e9, 1e9};
        QcVec3 high{-1e9, -1e9, -1e9};

        QString describe() const
        {
            return u"%1 ticks: %2 outside, %3 blocked, %4 flips, %5 cells, x %6..%7 z %8..%9"_s.arg(ticks)
                .arg(outside)
                .arg(blocked)
                .arg(flips)
                .arg(cells.size())
                .arg(low.x - origin.x)
                .arg(high.x - origin.x)
                .arg(low.z - origin.z)
                .arg(high.z - origin.z);
        }
        QcVec3 origin;
    };

    QcCreature *mob(quint32 id) { return m_server->entities().creatures().find(id); }
    QcCreatureKindId type(const char *name) const { return *m_content->mobKind(u"qtcraft:"_s + QString::fromLatin1(name)); }

    void addSeeds()
    {
        QTest::addColumn<qint64>("seed");
        for (const qint64 seed : {qint64{1}, qint64{79}, qint64{2026}, QcTestUtil::randomSeed()}) {
            QTest::newRow(qPrintable(u"seed %1"_s.arg(seed))) << seed;
        }
    }

    void start(qint64 seed)
    {
        QcServerConfig config;
        config.seed = seed;
        join(config);
    }

    // Water in the cells `water` (offsets from the feet) holds, closed in stone on all sides and
    // below, with air above. Built inside the box from `low` to `high` (offsets, inclusive).
    template <typename Water>
    void build(QcBlockPos low, QcBlockPos high, const Water &water)
    {
        for (int dx = low.x; dx <= high.x; ++dx) {
            for (int dz = low.z; dz <= high.z; ++dz) {
                for (int dy = low.y; dy <= high.y; ++dy) {
                    const QcBlockPos at{dx, dy, dz};
                    const QcBlockId block = water(at) ? id("qtcraft:water") : (dy < 0 ? id("qtcraft:stone") : qcAirBlockId);
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), block);
                }
            }
        }
    }

    quint32 spawnFish(const char *name, int dx, int dy, int dz)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, static_cast<double>(m_feet.y + dy), m_feet.z + dz + 0.5};
        return m_server->entities().creatures().spawn(type(name), at, m_server->entities().newId(), false, true);
    }

    // Whether the body of `fish` is out of the water: its centre in another block, or its top
    // above the surface of a cell with air over it.
    bool outside(const QcCreature &fish) const
    {
        const double height = m_content->mobs()[fish.type].height;
        const QcVec3 centre = fish.motion.position + QcVec3{0.0, height * 0.5, 0.0};
        if (!QcFishSwimming::inWater(m_server->world(), centre)) {
            return true;
        }
        const QcBlockPos top = QcPhysics::blockContaining(fish.motion.position + QcVec3{0.0, height, 0.0});
        return !QcPhysics::isWaterAt(m_server->world(), top) && fish.motion.position.y + height > top.y + 0.02;
    }

    // Steps `count` ticks and watches the fish `ids`.
    QList<Watch> watch(const QList<quint32> &ids, int count)
    {
        QList<Watch> watches(ids.size());
        QList<QcVec3> headings(ids.size());
        for (Watch &one : watches) {
            one.origin = QcVec3{static_cast<double>(m_feet.x), 0.0, static_cast<double>(m_feet.z)};
        }
        for (int tick = 0; tick < count; ++tick) {
            ticks(1);
            for (qsizetype i = 0; i < ids.size(); ++i) {
                const QcCreature *fish = mob(ids[i]);
                if (!fish) {
                    return {};
                }
                record(watches[i], headings[i], *fish);
            }
        }
        return watches;
    }

    void record(Watch &watch, QcVec3 &heading, const QcCreature &fish) const
    {
        ++watch.ticks;
        watch.outside += outside(fish) ? 1 : 0;
        watch.blocked += fish.blockedLastTick ? 1 : 0;
        const QcVec3 &p = fish.motion.position;
        watch.cells.insert(QcPhysics::blockContaining(p + QcVec3{0.0, 0.1, 0.0}));
        watch.low = {std::min(watch.low.x, p.x), std::min(watch.low.y, p.y), std::min(watch.low.z, p.z)};
        watch.high = {std::max(watch.high.x, p.x), std::max(watch.high.y, p.y), std::max(watch.high.z, p.z)};

        // A reversal: the heading of a fish that really moves turns by more than 120 degrees.
        const QcVec3 flat{fish.motion.velocity.x, 0.0, fish.motion.velocity.z};
        if (flat.length() < 0.3) {
            return;
        }
        const QcVec3 now = flat.normalized();
        if (heading.length() > 0.0 && now.x * heading.x + now.z * heading.z < -0.5) {
            ++watch.flips;
        }
        heading = now;
    }

    // What every fish has to keep to over `count` ticks: always in the water, hardly ever stopped
    // by a wall or the ground (only at the ends of its water), turning at most `maxFlips` times.
    static void verifyCalm(const Watch &one, int count, int maxFlips)
    {
        const QString text = one.describe();
        qInfo().noquote() << text;
        QVERIFY2(one.ticks == count, qPrintable(text));
        QVERIFY2(one.outside == 0, qPrintable(text));
        QVERIFY2(one.blocked <= count / 20, qPrintable(text));
        QVERIFY2(one.flips <= maxFlips, qPrintable(text));
    }

private slots:
    void fishSwimAlongANarrowCanal_data() { addSeeds(); }
    void fishSwimAlongANarrowCanal()
    {
        QFETCH(qint64, seed);
        start(seed);
        // One block wide, two deep, 16 long: x 5..20 at z 0, y -2..-1.
        build({4, -3, -1}, {21, 2, 1}, [](QcBlockPos at) { return at.x >= 5 && at.x <= 20 && at.z == 0 && at.y >= -2 && at.y <= -1; });
        const QList<quint32> fish{spawnFish("cod", 8, -2, 0), spawnFish("cod", 12, -1, 0), spawnFish("salmon", 16, -2, 0)};

        // Swimming along a wall is no reason to stop; it turns at its goals, not tick by tick, and
        // gets around.
        const QList<Watch> watches = watch(fish, 1200);
        QCOMPARE(watches.size(), fish.size());
        for (const Watch &one : watches) {
            verifyCalm(one, 1200, 40);
            QVERIFY2(one.high.x - one.low.x >= 6.0, qPrintable(one.describe()));
        }
    }

    void aFleeingFishDartsAwayAlongTheCanal_data() { addSeeds(); }
    void aFleeingFishDartsAwayAlongTheCanal()
    {
        QFETCH(qint64, seed);
        start(seed);
        // One block wide and deep, 16 long: x 5..20 at z 0, y -1.
        build({4, -2, -1}, {21, 2, 1}, [](QcBlockPos at) { return at.y == -1 && at.x >= 5 && at.x <= 20 && at.z == 0; });
        const QList<quint32> fish{spawnFish("salmon", 7, -1, 0)};
        ticks(1);

        // Struck from the west (as QcMobManager leaves it): it flees east, reaches its goal and
        // rests there instead of turning about.
        mob(fish[0])->fleeTimer = 3.0;
        mob(fish[0])->wanderDirection = {1.0, 0.0, 0.0};
        const Watch one = watch(fish, 60).value(0);
        verifyCalm(one, 60, 0);
        QVERIFY2(one.high.x - m_feet.x >= 11.0, qPrintable(one.describe()));
    }

    void fishFollowACanalAroundACorner_data() { addSeeds(); }
    void fishFollowACanalAroundACorner()
    {
        QFETCH(qint64, seed);
        start(seed);
        // An L one block wide and deep: x 5..14 at z 0, then z 0..9 at x 14, y -1.
        build({4, -2, -1}, {15, 2, 10}, [](QcBlockPos at) {
            return at.y == -1 && ((at.z == 0 && at.x >= 5 && at.x <= 14) || (at.x == 14 && at.z >= 0 && at.z <= 9));
        });
        const QList<quint32> fish{spawnFish("cod", 6, -1, 0)};

        const Watch one = watch(fish, 2400).value(0);
        verifyCalm(one, 2400, 80);
        // Into the other arm.
        QVERIFY2(one.high.z - m_feet.z >= 5.0, qPrintable(one.describe()));
    }

    void fishSwimInAShallowPool_data() { addSeeds(); }
    void fishSwimInAShallowPool()
    {
        QFETCH(qint64, seed);
        start(seed);
        // One block deep, 8 x 8: x 5..12, z -4..3, y -1.
        build({4, -2, -5}, {13, 2, 4}, [](QcBlockPos at) { return at.y == -1 && at.x >= 5 && at.x <= 12 && at.z >= -4 && at.z <= 3; });
        const QList<quint32> fish{spawnFish("cod", 6, -1, -3), spawnFish("cod", 9, -1, 0), spawnFish("salmon", 11, -1, 2)};

        // Not scraping along the ground, and all over the pool.
        const QList<Watch> watches = watch(fish, 1200);
        QCOMPARE(watches.size(), fish.size());
        for (const Watch &one : watches) {
            verifyCalm(one, 1200, 40);
            QVERIFY2(one.cells.size() >= 8, qPrintable(one.describe()));
        }
    }

    void aFishInATinyHoleIdles_data() { addSeeds(); }
    void aFishInATinyHoleIdles()
    {
        QFETCH(qint64, seed);
        start(seed);
        // A single cell of water.
        build({5, -2, -1}, {7, 2, 1}, [](QcBlockPos at) { return at.y == -1 && at.x == 6 && at.z == 0; });
        const QList<quint32> fish{spawnFish("cod", 6, -1, 0)};

        verifyCalm(watch(fish, 600).value(0), 600, 2);
        QCOMPARE(mob(fish[0])->dryTicks, 0);
    }
};

QTEST_GUILESS_MAIN(TestFishPaths)
#include "test_fish_paths.moc"
