// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcBoatMotion.h"
#include "QcServerTestFixture.h"
#include "server/boats/QcServerBoats.h"
#include "server/entities/QcEntityChecks.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// Headless measurement of boats (decision E-920), ticked by hand as fast as the server goes: boats on
// a walled pond high above the stage, two metres apart, with pigs (without a mind) standing on a
// platform beside it. First every boat drifts towards its neighbour (kept at the same speed between
// ticks, outside the measured phase), so the boats push each other; then they are left alone, come
// to rest and are measured standing. Prints the time of the vehicle phase (minecarts and boats) and
// of the whole tick per tick. Small in every test run (64 boats); QTCRAFT_BOAT_MEASURE=full measures
// 256, 1024 and 4096 boats (the default limit) with 256 pigs.
class TestBoatMeasure : public QcServerTestFixture
{
    Q_OBJECT

private:
    struct Size
    {
        int boats = 64;
        int pigs = 16;
        int ticks = 20;
    };

    static bool full() { return qEnvironmentVariable("QTCRAFT_BOAT_MEASURE") == u"full"_s; }

    QcBlockPos m_origin;
    int m_side = 0;

    // A pond one block deep with `side` x `side` blocks of water, walled with stone, and a stone
    // platform west of it for the pigs.
    void buildPond(int side)
    {
        m_side = side;
        m_origin = m_feet.offset(-side / 2, 20, -side / 2);
        for (int x = -24; x <= side + 24; x += 8) {
            for (int z = -24; z <= side + 24; z += 8) {
                m_server->world().loadChunk(m_origin.offset(x, 0, z).chunk());
            }
        }
        for (int x = -1; x <= side; ++x) {
            for (int z = -1; z <= side; ++z) {
                const bool wall = x < 0 || z < 0 || x == side || z == side;
                QVERIFY(m_server->world().setBlock(m_origin.offset(x, -1, z), id("qtcraft:stone")));
                QVERIFY(m_server->world().setBlock(m_origin.offset(x, 0, z), id(wall ? "qtcraft:stone" : "qtcraft:water")));
            }
        }
        for (int x = -20; x <= -4; ++x) {
            for (int z = 0; z < 16; ++z) {
                QVERIFY(m_server->world().setBlock(m_origin.offset(x, 0, z), id("qtcraft:stone")));
            }
        }
    }

    void placeBoats(int count)
    {
        const int perRow = m_side / 2;
        const double floating = m_origin.y + 1.0 - QcBoatMotion::liquidSurfaceDrop - QcBoatMotion::draft;
        for (int i = 0; i < count; ++i) {
            QcBoat boat;
            boat.item = u"qtcraft:oak_boat"_s;
            const int column = i % perRow;
            const int row = i / perRow;
            boat.motion.position = {m_origin.x + 1.0 + 2.0 * column, floating, m_origin.z + 1.0 + 2.0 * row};
            QVERIFY(m_server->systems().boats.spawn(boat));
        }
    }

    void placePigs(int count)
    {
        const auto it = std::ranges::find(m_content->mobs(), u"qtcraft:pig"_s, &QcMobDefinition::name);
        const int type = static_cast<int>(it - m_content->mobs().begin());
        for (int i = 0; i < count; ++i) {
            const QcVec3 at{m_origin.x - 19.5 + (i % 16), m_origin.y + 1.0, m_origin.z + 0.5 + (i / 16) % 16};
            const quint32 pig = m_server->entities().creatures().spawn(type, at, m_server->entities().newId());
            m_server->entities().creatures().find(pig)->noAi = true;
        }
    }

    // Every boat drifts towards its neighbour in the row (odd and even ones towards each other).
    void kick()
    {
        qsizetype index = 0;
        for (QcBoat &boat : m_server->entities().boats().editableAll()) {
            boat.motion.velocity = {index % 2 == 0 ? 0.8 : -0.8, 0.0, 0.0};
            ++index;
        }
    }

    struct Times
    {
        double phaseMeanMs = 0.0;
        double phaseMaxMs = 0.0;
        double tickMeanMs = 0.0;
    };

    Times run(int ticks, bool drifting)
    {
        Times times;
        for (int i = 0; i < ticks; ++i) {
            if (drifting) {
                kick();
            }
            QElapsedTimer timer;
            timer.start();
            m_server->tick();
            const double tickMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
            const double phaseMs = static_cast<double>(m_server->tickPhases().lastTick()[static_cast<std::size_t>(QcTickPhase::Minecarts)]) / 1e6;
            times.phaseMeanMs += phaseMs / ticks;
            times.phaseMaxMs = std::max(times.phaseMaxMs, phaseMs);
            times.tickMeanMs += tickMs / ticks;
        }
        return times;
    }

    static void report(const char *scenario, const Times &times, int boats)
    {
        qInfo("MEASURE boats %s boats=%d phase_mean_ms=%.4f phase_max_ms=%.4f tick_mean_ms=%.4f", scenario, boats, times.phaseMeanMs,
              times.phaseMaxMs, times.tickMeanMs);
    }

private slots:
    void boatsOnAPond_data()
    {
        QTest::addColumn<int>("boats");
        QTest::addColumn<int>("pigs");
        QTest::addColumn<int>("ticks");
        if (!full()) {
            const Size small;
            QTest::newRow("small") << small.boats << small.pigs << small.ticks;
            return;
        }
        for (const int boats : {256, 1024, 4096}) {
            QTest::newRow(qPrintable(QString::number(boats))) << boats << 256 << 50;
        }
    }

    void boatsOnAPond()
    {
        QFETCH(int, boats);
        QFETCH(int, pigs);
        QFETCH(int, ticks);
        join();
        chat(u"/gamerule mobSpawning false"_s);
        const int perRow = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(boats))));
        buildPond(2 * perRow + 2);
        placeBoats(boats);
        placePigs(pigs);
        QCOMPARE(m_server->entities().boats().all().size(), qsizetype(boats));

        const Times drifting = run(ticks, true);
        report("drifting", drifting, boats);

        // Left alone, they settle on the water; then they cost what floating boats cost.
        run(200, false);
        const Times resting = run(ticks, false);
        report("resting", resting, boats);

        QCOMPARE(m_server->entities().boats().all().size(), qsizetype(boats));
        const double floating = m_origin.y + 1.0 - QcBoatMotion::liquidSurfaceDrop - QcBoatMotion::draft;
        QVERIFY(std::ranges::all_of(m_server->entities().boats().all(), [floating](const QcBoat &boat) {
            return boat.motion.inWater && std::abs(boat.motion.position.y - floating) < 1e-3;
        }));
        QVERIFY(QcEntityChecks::problems(m_server->entities()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestBoatMeasure)
#include "test_boat_measure.moc"
