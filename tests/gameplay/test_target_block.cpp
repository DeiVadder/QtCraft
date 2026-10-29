// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcPlayerUseFixture.h"
#include "server/signal/QcServerCircuits.h"

using namespace Qt::StringLiterals;

// The target block (decision E-768): a projectile hitting it gives a signal of 1..15 by how close to
// the middle of the face it hit, for 20 ticks after an arrow and 8 after a snowball; dust and
// components around it get the signal, no block does; arrows stay in it while it switches.
class TestTargetBlock : public QcPlayerUseFixture
{
    Q_OBJECT

private:
    // Shoots `type` from just west of `target`'s west face at the point (0, y, z) of that face (close,
    // so that it hardly drops on the way).
    void shootAt(QcBlockPos target, const char *type, double y, double z)
    {
        const QcVec3 from{target.x - 0.6, target.y + y, target.z + z};
        QVERIFY(m_server->systems().projectiles.launch({player().id, QString::fromLatin1(type), from, {30.0, 0.0, 0.0}}));
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void craftedFromHayAndChunks()
    {
        buildArea();
        QCOMPARE(crafted({{1, u"signal_dust_chunk"_s}, {3, u"signal_dust_chunk"_s}, {4, u"hay_block"_s}, {5, u"signal_dust_chunk"_s},
                          {7, u"signal_dust_chunk"_s}},
                         3),
                 (std::pair{u"qtcraft:target"_s, 1}));
        QCOMPARE(crafted({{4, u"hay_block"_s}, {1, u"signal_dust"_s}}, 3), (std::pair<QString, int>{}));
    }

    void strengthByDistanceFromTheMiddle()
    {
        const QcBlockPos pos{10, 64, -3};
        const auto strength = [pos](QcBlockFace face, double a, double b) {
            const bool sideX = face == QcBlockFace::West || face == QcBlockFace::East;
            const QcVec3 point = face == QcBlockFace::Up ? QcVec3{pos.x + a, pos.y + 1.0, pos.z + b}
                                 : sideX                 ? QcVec3{static_cast<double>(pos.x), pos.y + a, pos.z + b}
                                                         : QcVec3{pos.x + a, pos.y + b, static_cast<double>(pos.z)};
            return QcServerCircuits::targetStrength(pos, face, point);
        };
        QCOMPARE(strength(QcBlockFace::West, 0.5, 0.5), 15);
        QCOMPARE(strength(QcBlockFace::North, 0.5, 0.5), 15);
        QCOMPARE(strength(QcBlockFace::Up, 0.5, 0.5), 15);
        // A quarter block off the middle: half way to the edge.
        QCOMPARE(strength(QcBlockFace::West, 0.75, 0.5), 8);
        QCOMPARE(strength(QcBlockFace::Up, 0.5, 0.25), 8);
        // The larger of the two distances counts; the edge and beyond give 1.
        QCOMPARE(strength(QcBlockFace::North, 0.6, 0.98), 1);
        QCOMPARE(strength(QcBlockFace::West, 0.0, 0.5), 1);
        QCOMPARE(strength(QcBlockFace::West, 0.5, 0.45), 14);
    }

    void anArrowInTheMiddleGivesFifteenForASecond()
    {
        buildArea();
        put(5, 0, 5, u"target"_s);
        put(6, 0, 5, u"signal_dust"_s);
        put(5, 0, 7, u"target"_s);
        put(6, 0, 7, u"signal_dust"_s);
        put(5, 1, 5, u"signal_lamp"_s);
        ticks(2);

        shootAt(at(5, 0, 5), "qtcraft:arrow", 0.5, 0.5);
        ticks(3);
        QCOMPARE(property(5, 0, 5, u"power"_s), u"15"_s);
        QCOMPARE(power(6, 0, 5), 15);
        QCOMPARE(power(6, 0, 7), 0);
        // A lamp on it lights directly; the target itself passes nothing on as a block.
        QVERIFY(isBlock(5, 1, 5, "signal_lamp_lit"));
        QCOMPARE(m_server->entities().projectiles().all().size(), 1);
        QVERIFY(m_server->entities().projectiles().all().front().stuck);

        ticks(20);
        QCOMPARE(property(5, 0, 5, u"power"_s), u"0"_s);
        QCOMPARE(power(6, 0, 5), 0);
        // The arrow stays in the target through both changes.
        QCOMPARE(m_server->entities().projectiles().all().size(), 1);
        QVERIFY(m_server->entities().projectiles().all().front().stuck);
    }

    void aSnowballNearTheEdgeGivesLittleAndBriefly()
    {
        buildArea();
        put(5, 0, 5, u"target"_s);
        ticks(2);
        shootAt(at(5, 0, 5), "qtcraft:snowball", 0.5, 0.9);
        ticks(3);
        const int strength = property(5, 0, 5, u"power"_s).toInt();
        QVERIFY2(strength >= 1 && strength <= 4, qPrintable(QString::number(strength)));
        ticks(8);
        QCOMPARE(property(5, 0, 5, u"power"_s), u"0"_s);
    }

    void aNewHitStartsTheTimeAgain()
    {
        buildArea();
        put(5, 0, 5, u"target"_s);
        ticks(2);
        QVERIFY(m_server->systems().circuits.hitTarget(at(5, 0, 5), 4, true));
        ticks(15);
        QVERIFY(m_server->systems().circuits.hitTarget(at(5, 0, 5), 9, true));
        ticks(10);
        QCOMPARE(property(5, 0, 5, u"power"_s), u"9"_s);
        ticks(12);
        QCOMPARE(property(5, 0, 5, u"power"_s), u"0"_s);
        QVERIFY(!m_server->systems().circuits.hitTarget(at(6, 0, 5), 9, true));
    }
};

QTEST_GUILESS_MAIN(TestTargetBlock)
#include "test_target_block.moc"
