// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcServerTestFixture.h"

#include <QElapsedTimer>

#include <algorithm>
#include <numbers>

using namespace Qt::StringLiterals;

// What the Kjut costs on the server (decision E-839): 16 Kjut families (each parent with four
// children split off by hits, so 80 hoverers) against 16 zombies, both chasing a survival player at
// night on a flat floor (the player is healed every tick so the chase never ends). Rounds alternate
// between the two, each a fresh arena; the ticks are stepped by hand and only the server's tick is
// timed. Every test run does a short round; QTCRAFT_KJUT_MEASURE=1 measures (Release build:
// `build/release/tests/test_kjut_measure`) and prints one line per round:
// "MEASURE kjut <kind> round <n>: <creatures> creatures, server tick <ms> ms (<us> us per creature)".
class TestKjutMeasure : public QcServerTestFixture
{
    Q_OBJECT

    static constexpr int families = 16;
    static constexpr int childrenEach = 4;

    int kind(const QString &name) const { return *m_content->mobIndex(name); }

    QcServerClient &player() { return *m_server->entities().clients().front(); }

    // A floor of grass 41 x 41 blocks around the player with air above, night, no natural spawning.
    void buildArena()
    {
        chat(u"/gamemode survival"_s);
        chat(u"/time set night"_s);
        m_server->mobs().setNaturalSpawning(false);
        m_server->mobs().removeAll();
        for (int dx = -20; dx <= 20; ++dx) {
            for (int dz = -20; dz <= 20; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
                for (int dy = 0; dy <= 4; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
    }

    // `families` creatures of `type` on a ring of 12 m around the player.
    QList<quint32> spawnRing(int type)
    {
        QList<quint32> ids;
        for (int i = 0; i < families; ++i) {
            const double angle = 2.0 * std::numbers::pi * i / families;
            const QcVec3 at{m_feet.x + 0.5 + 12.0 * std::cos(angle), static_cast<double>(m_feet.y), m_feet.z + 0.5 + 12.0 * std::sin(angle)};
            ids.append(m_server->mobs().spawn(type, at, m_server->entities().newId()));
        }
        return ids;
    }

    // One tick with the player standing still and healed; the nanoseconds of the server's tick.
    qint64 step()
    {
        player().health = QcServerClient::maxHealth;
        QcPlayerInput input;
        input.sequence = ++m_sequence;
        m_client->connection.send(QcMsg::PlayerInput{input});
        QcTestUtil::pump();
        QElapsedTimer timer;
        timer.start();
        ticks(1);
        return timer.nsecsElapsed();
    }

    // A round of `count` measured ticks with `name`'s creatures after a warm-up; the server tick in ms.
    double round(const QString &name, int count)
    {
        buildArena();
        const QList<quint32> parents = spawnRing(kind(name));
        if (name == u"qtcraft:kjut"_s) {
            // A hit after each invulnerability splits one child off every parent.
            for (int hit = 0; hit < childrenEach; ++hit) {
                for (const quint32 id : parents) {
                    m_server->mobs().wound(id, 1.0);
                }
                for (int i = 0; i < 11; ++i) {
                    step();
                }
            }
        }
        for (int i = 0; i < 40; ++i) {
            step();
        }
        qint64 nanoseconds = 0;
        for (int i = 0; i < count; ++i) {
            nanoseconds += step();
        }
        return static_cast<double>(nanoseconds) / 1e6 / count;
    }

private slots:
    void kjutsAgainstZombies()
    {
        const bool measuring = qEnvironmentVariableIntValue("QTCRAFT_KJUT_MEASURE") == 1;
        const int rounds = measuring ? 5 : 1;
        const int count = measuring ? 600 : 20;
        join();
        for (int r = 0; r < rounds; ++r) {
            // Alternating which goes first, so drift of the machine falls on both.
            QStringList order{u"qtcraft:kjut"_s, u"qtcraft:zombie"_s};
            if (r % 2 == 1) {
                std::ranges::reverse(order);
            }
            for (const QString &name : std::as_const(order)) {
                const double ms = round(name, count);
                // Generated chunks may bring fish of their own; only the measured kinds count.
                const qsizetype creatures = std::ranges::count_if(m_server->mobs().mobs(), [&](const QcCreature &mob) {
                    return mob.type == kind(name) || mob.type == kind(u"qtcraft:kjut_child"_s);
                });
                qInfo("MEASURE kjut %s round %d: %lld creatures, server tick %.3f ms (%.2f us per creature)", qPrintable(name), r,
                      static_cast<qlonglong>(creatures), ms, ms * 1000.0 / static_cast<double>(creatures));
                const qsizetype expected = name == u"qtcraft:kjut"_s ? families * (1 + childrenEach) : families;
                QCOMPARE(creatures, expected);
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestKjutMeasure)
#include "test_kjut_measure.moc"
