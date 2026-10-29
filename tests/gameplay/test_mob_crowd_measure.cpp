// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcDayCycle.h"
#include "QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "server/pathing/QcPathPlanner.h"
#include "worldgen/generators/QcFlatGenerator.h"

#if __has_include("QcMobCrowd.h")
#include "server/mobs/QcMobBehaviour.h"
#include "server/mobs/QcServerCreatures.h"
#define QC_HAS_CROWDS 1
#endif
#if __has_include("QcMobCramming.h")
#include "server/mobs/QcMobCramming.h"
#define QC_HAS_CRAMMING 1
#endif

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// Cost of the creature tick (the server's "mobs" phase) with crowds (decision E-401): 1000 zombies
// in one 1 x 1 spawner cell (also refilled to 1000 before every tick), 1000 spread over a flat
// field around a player, and a mob farm where zombies drop down a shaft into a 1 x 1 killing cell
// (with the cramming of the game rule maxEntityCramming 24 where the code has it), and players
// pushed by a spread crowd (decision E-882). A measurement, not a check: set
// QTCRAFT_CROWD_MEASURE=1 and use a Release build. The ticks run as fast as they can, not in real
// time. Without QcMobCrowd the file uses only what QcMobManager offered in stage 37.1a, so the same
// file measures the state before the crowds.
class TestMobCrowdMeasure : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    quint32 m_nextId = 100;

    static constexpr double midnight = QcDayCycle::dayLength * 0.75;
    static constexpr int ground = 5;
    static constexpr int radius = 3;

    std::unique_ptr<QcServerWorld> flat() const
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcFlatGenerator>(m_content, u"bedrock,3*stone,grass_block"_s));
        for (int x = -radius; x <= radius; ++x) {
            for (int z = -radius; z <= radius; ++z) {
                world->loadChunk({x, z});
            }
        }
        return world;
    }

    // Stone around the column x 0, z 0 from the ground up to `top`.
    void shaft(QcServerWorld &world, int top) const
    {
        const QcBlockId stone = *m_content->blockId(u"qtcraft:stone"_s);
        for (int y = ground; y <= top; ++y) {
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    world.setBlock({dx, y, dz}, dx == 0 && dz == 0 ? qcAirBlockId : stone);
                }
            }
        }
    }

    // The cramming of QcServerCreatures (game rule 24) on the manager alone; as QcServerCreatures
    // did it in stage 37.1b before QcMobCramming; nothing before the crowds.
    int cram(QcMobManager &mobs, quint64 tick) const
    {
        int died = 0;
#if defined(QC_HAS_CRAMMING)
        const QList<QcMobCramming::Hurt> hurt = QcMobCramming::apply(mobs, m_cramming, tick);
        for (const QcMobCramming::Hurt &one : hurt) {
            died += one.drops ? 1 : 0;
        }
#elif defined(QC_HAS_CROWDS)
        QHash<int, int> diedIn;
        const QList<QcMobCrowd::Crammed> crammed = mobs.crammed(m_cramming);
        for (const QcMobCrowd::Crammed &one : crammed) {
            const QcCreature *mob = mobs.find(one.id);
            if ((tick + one.id) % QcServerCreatures::crammingPhases != 0 || !mob || mob->hurt.active()
                || one.count - diedIn.value(one.cell) <= m_cramming) {
                continue;
            }
            if (mobs.wound(one.id, QcServerCreatures::crammingDamage)) {
                ++died;
                diedIn[one.cell] += 1;
            }
        }
#else
        Q_UNUSED(mobs);
        Q_UNUSED(tick);
#endif
        return died;
    }

    // The game rule maxEntityCramming of the run.
    int m_cramming = 24;

    struct Run
    {
        double total = 0.0;
        double worst = 0.0;
        int ticks = 0;
        int died = 0;
    };

    void tickTimed(QcMobManager &mobs, QcServerWorld &world, const QList<QcMobManager::PlayerView> &players, quint64 tick, Run &run)
    {
        QElapsedTimer timer;
        timer.start();
        mobs.tick(world, players, midnight, 0.05, [this] { return m_nextId++; });
        run.died += cram(mobs, tick);
        const double ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        run.total += ms;
        run.worst = std::max(run.worst, ms);
        ++run.ticks;
    }

    void report(const char *name, const QcMobManager &mobs, const Run &run) const
    {
        QString tiers = u"-"_s;
#ifdef QC_HAS_CROWDS
        const QcMobBehaviour::TierCounts counts = mobs.behaviour().tiersLastTick();
        tiers = u"%1/%2/%3"_s.arg(counts.full).arg(counts.throttled).arg(counts.idle);
#endif
        qInfo("MEASURE %s: %.3f ms mean, %.3f ms max per tick, %lld creatures at the end, %d crammed to death, %lld searches, "
              "tiers full/throttled/idle %s",
              name, run.total / run.ticks, run.worst, static_cast<qlonglong>(mobs.mobs().size()), run.died, static_cast<qlonglong>(mobs.paths().searches()),
              qPrintable(tiers));
    }

    std::unique_ptr<QcMobManager> makeMobs() const
    {
        auto mobs = std::make_unique<QcMobManager>(m_content, 7);
        mobs->setNaturalSpawning(false);
        return mobs;
    }

private slots:
    void initTestCase()
    {
        if (qEnvironmentVariableIntValue("QTCRAFT_CROWD_MEASURE") != 1) {
            QSKIP("measurement: set QTCRAFT_CROWD_MEASURE=1");
        }
    }

    // 1000 zombies in one walled cell, a player five blocks away who they cannot see; without
    // cramming (all stay) and with it (24 stay).
    void spawnerCell()
    {
        m_cramming = 0;
        measureCell("cell", false);
        m_cramming = 24;
        measureCell("cell-cramming", false);
        // A spawner that fills the cell up to 1000 again before every tick: about a thousand crammed
        // creatures each tick.
        measureCell("cell-refill", true);
    }

private:
    void measureCell(const char *name, bool refill)
    {
        auto world = flat();
        shaft(*world, ground + 2);
        auto mobs = makeMobs();
        const int zombie = *m_content->mobIndex(u"qtcraft:zombie"_s);
        for (int i = 0; i < 1000; ++i) {
            mobs->spawn(zombie, {0.5, static_cast<double>(ground), 0.5}, m_nextId++);
        }
        const QList<QcMobManager::PlayerView> players{{7, {5.5, static_cast<double>(ground), 0.5}, true}};
        Run run;
        for (quint64 tick = 0; tick < 220; ++tick) {
            while (refill && mobs->mobs().size() < 1000) {
                mobs->spawn(zombie, {0.5, static_cast<double>(ground), 0.5}, m_nextId++);
            }
            Run warmUp;
            tickTimed(*mobs, *world, players, tick, tick < 20 ? warmUp : run);
        }
        report(name, *mobs, run);
    }

private slots:

    // 1000 zombies spread over 80 x 80 blocks around a player who walks a circle.
    void spread()
    {
        auto world = flat();
        auto mobs = makeMobs();
        const int zombie = *m_content->mobIndex(u"qtcraft:zombie"_s);
        for (int i = 0; i < 1000; ++i) {
            const int x = static_cast<int>((i * 7919u) % 80u) - 40;
            const int z = static_cast<int>((i * 104729u / 80u) % 80u) - 40;
            mobs->spawn(zombie, {x + 0.5, static_cast<double>(ground), z + 0.5}, m_nextId++);
        }
        Run run;
        for (quint64 tick = 0; tick < 220; ++tick) {
            const double angle = static_cast<double>(tick) * 0.025;
            const QList<QcMobManager::PlayerView> players{{7, {4.0 * std::cos(angle), static_cast<double>(ground), 4.0 * std::sin(angle)}, true}};
            Run warmUp;
            tickTimed(*mobs, *world, players, tick, tick < 20 ? warmUp : run);
        }
        report("spread", *mobs, run);
    }

    // The push of the creatures on players (decision E-882): 1000 zombies spread as above and 8
    // players walking circles among them; each tick the creatures move, then every player's input
    // asks for his push (the grid is built once per tick).
    void playersInACrowd()
    {
#if __has_include("server/mobs/QcCreatureBodies.h")
        auto world = flat();
        auto mobs = makeMobs();
        const int zombie = *m_content->mobIndex(u"qtcraft:zombie"_s);
        for (int i = 0; i < 1000; ++i) {
            const int x = static_cast<int>((i * 7919u) % 80u) - 40;
            const int z = static_cast<int>((i * 104729u / 80u) % 80u) - 40;
            mobs->spawn(zombie, {x + 0.5, static_cast<double>(ground), z + 0.5}, m_nextId++);
        }
        Run run;
        double pushMs = 0.0;
        double pushed = 0.0;
        for (quint64 tick = 0; tick < 220; ++tick) {
            QList<QcMobManager::PlayerView> players;
            for (quint32 p = 0; p < 8; ++p) {
                const double angle = static_cast<double>(tick) * 0.025 + static_cast<double>(p) * 0.785;
                const double r = 4.0 + 4.0 * static_cast<double>(p);
                players.append({7 + p, {r * std::cos(angle), static_cast<double>(ground), r * std::sin(angle)}, true});
            }
            Run warmUp;
            tickTimed(*mobs, *world, players, tick, tick < 20 ? warmUp : run);
            QElapsedTimer timer;
            timer.start();
            for (const QcMobManager::PlayerView &player : std::as_const(players)) {
                pushed += mobs->pushOnPlayer(player.position, tick).length();
            }
            pushMs += tick < 20 ? 0.0 : static_cast<double>(timer.nsecsElapsed()) / 1e6;
        }
        report("players-in-crowd", *mobs, run);
        qInfo("MEASURE players-in-crowd push: %.4f ms per tick for 8 players (sum of pushes %.2f)", pushMs / run.ticks, pushed);
#else
        QSKIP("no push between players and creatures in this build");
#endif
    }

    // A farm: four zombies a second appear at the top of a shaft 20 blocks high and drop into the
    // 1 x 1 cell at its foot, 60 s (1200 ticks); the player waits ten blocks away behind the stone.
    void farm()
    {
        auto world = flat();
        shaft(*world, ground + 22);
        auto mobs = makeMobs();
        const int zombie = *m_content->mobIndex(u"qtcraft:zombie"_s);
        const QList<QcMobManager::PlayerView> players{{7, {10.5, static_cast<double>(ground), 0.5}, true}};
        Run run;
        for (quint64 tick = 0; tick < 1200; ++tick) {
            if (tick % 5 == 0) {
                mobs->spawn(zombie, {0.5, static_cast<double>(ground + 20), 0.5}, m_nextId++);
            }
            tickTimed(*mobs, *world, players, tick, run);
        }
        report("farm", *mobs, run);
    }
};

QTEST_GUILESS_MAIN(TestMobCrowdMeasure)
#include "test_mob_crowd_measure.moc"
