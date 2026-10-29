// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcPushBodies.h"
#include "common/physics/QcBodyPush.h"
#include "QcTestUtil.h"

#include <QTest>

using namespace Qt::StringLiterals;

// The client's mirror of the creatures that push the local player (QcPushBodies, decision E-882):
// it follows spawns, moves (with their poses) and removals, leaves out lying creatures and flyers,
// and moves walking creatures on along their last step by the lead of the input.
class TestPushBodies : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    // The player's feet; the creatures stand around them.
    const QcVec3 m_feet{0.5, 64.0, 0.5};

    int type(const QString &name) const
    {
        const std::optional<int> found = m_content->mobIndex(name);
        if (!found) {
            QcTestUtil::failCase(u"Test uses the creature %1, which the content does not have"_s.arg(name));
            return 0;
        }
        return *found;
    }

    static QcMsg::EntitySpawn spawn(quint32 id, int type, const QcVec3 &position, QcPose pose = QcPose::Standing)
    {
        QcMsg::EntitySpawn message;
        message.id = id;
        message.kind = QcEntityKind::Creature;
        message.creatureType = type;
        message.position = position;
        message.flags = QcMsg::poseFlags(pose);
        return message;
    }

    static QcMsg::EntityMoves move(quint32 id, const QcVec3 &position, QcPose pose = QcPose::Standing)
    {
        QcMsg::EntityMove one;
        one.id = id;
        one.position = position;
        one.flags = QcMsg::poseFlags(pose);
        return {{one}, 0};
    }

    // The push of one creature of `name` with its feet at `feet` on the player, as the server's
    // formula gives it.
    QcVec3 expected(const QString &name, const QcVec3 &feet) const
    {
        const QcMobDefinition &definition = m_content->mobs()[type(name)];
        return QcBodyPush::playerVelocity(QcBodyPush::between(QcBodyPush::player(m_feet), QcBodyPush::creature(definition, feet)));
    }

    QcVec3 pushOnPlayer(const QcPushBodies &bodies, int leadTicks = 0) const { return bodies.pushOnPlayer(m_feet, *m_content, leadTicks); }

private slots:
    void followsSpawnsMovesPosesAndRemovals()
    {
        QcPushBodies bodies;
        QCOMPARE(pushOnPlayer(bodies), QcVec3{});

        // A villager a little east of the player pushes him west.
        const QcVec3 near = m_feet + QcVec3{0.2, 0.0, 0.0};
        bodies.apply(spawn(7, type(u"qtcraft:villager"_s), near));
        QVERIFY(pushOnPlayer(bodies).x < 0.0);
        QCOMPARE(pushOnPlayer(bodies), expected(u"qtcraft:villager"_s, near));

        // Lying in bed (decision E-411) it does not push, also on the same spot.
        bodies.apply(move(7, near, QcPose::Lying));
        QCOMPARE(pushOnPlayer(bodies), QcVec3{});
        // Sitting it does.
        bodies.apply(move(7, near, QcPose::Sitting));
        QCOMPARE(pushOnPlayer(bodies), expected(u"qtcraft:villager"_s, near));

        // Moves of other entities and other messages change nothing.
        bodies.apply(move(8, m_feet));
        bodies.apply(QcMsg::EntityRemove{8, false});
        QCOMPARE(pushOnPlayer(bodies), expected(u"qtcraft:villager"_s, near));

        bodies.apply(QcMsg::EntityRemove{7, true});
        QCOMPARE(pushOnPlayer(bodies), QcVec3{});
    }

    void lyingSpawnsAndFlyersDoNotPush()
    {
        QcPushBodies bodies;
        bodies.apply(spawn(1, type(u"qtcraft:villager"_s), m_feet + QcVec3{0.1, 0.0, 0.0}, QcPose::Lying));
        bodies.apply(spawn(2, type(u"qtcraft:firefly"_s), m_feet + QcVec3{0.0, 0.5, 0.05}));
        QCOMPARE(pushOnPlayer(bodies), QcVec3{});
        // Not a creature: no body that pushes.
        QcMsg::EntitySpawn item = spawn(3, 0, m_feet);
        item.kind = QcEntityKind::Item;
        bodies.apply(item);
        QCOMPARE(pushOnPlayer(bodies), QcVec3{});
    }

    void movesAWalkerOnAlongItsLastStep()
    {
        QcPushBodies bodies;
        // A cow walking west towards the player, 0.1 m per tick.
        const QcVec3 step{-0.1, 0.0, 0.0};
        QcVec3 cow = m_feet + QcVec3{1.2, 0.0, 0.0};
        bodies.apply(spawn(1, type(u"qtcraft:cow"_s), cow));
        // Before its first move it has no step: it stays where it is.
        QCOMPARE(pushOnPlayer(bodies, 5), QcVec3{});
        for (int i = 0; i < 3; ++i) {
            cow = cow + step;
            bodies.apply(move(1, cow));
        }
        // Out of reach where it is, in reach five ticks on.
        QCOMPARE(pushOnPlayer(bodies, 0), QcVec3{});
        const QcVec3 ahead = pushOnPlayer(bodies, 5);
        QVERIFY(ahead.x < 0.0);
        const QcVec3 inFive = cow + step * 5.0;
        QVERIFY((ahead - expected(u"qtcraft:cow"_s, inFive)).length() < 1e-12);

        // At most maxLeadTicks ticks and maxLead metres on.
        const QcVec3 capped = pushOnPlayer(bodies, 50);
        const QcVec3 inTen = cow + step * static_cast<double>(QcPushBodies::maxLeadTicks);
        QVERIFY((capped - expected(u"qtcraft:cow"_s, inTen)).length() < 1e-12);

        // A step longer than maxLead (a teleport) is no walk: the creature stays where it landed.
        const QcVec3 landed = m_feet + QcVec3{0.3, 0.0, 0.0};
        bodies.apply(move(1, landed + QcVec3{5.0, 0.0, 0.0}));
        bodies.apply(move(1, landed));
        QVERIFY((pushOnPlayer(bodies, 5) - expected(u"qtcraft:cow"_s, landed)).length() < 1e-12);

        // Standing still again, its step is zero.
        bodies.apply(move(1, landed));
        QCOMPARE(pushOnPlayer(bodies, 5), expected(u"qtcraft:cow"_s, landed));
    }

    void aFastStepIsMovedOnAtMostMaxLead()
    {
        QcPushBodies bodies;
        // 0.5 m per tick: ten ticks would be 5 m, but it goes no farther than maxLead.
        QcVec3 cow = m_feet + QcVec3{1.5, 0.0, 0.0};
        bodies.apply(spawn(1, type(u"qtcraft:cow"_s), cow));
        cow = cow + QcVec3{-0.5, 0.0, 0.0};
        bodies.apply(move(1, cow));
        const QcVec3 capped = pushOnPlayer(bodies, 10);
        QVERIFY((capped - expected(u"qtcraft:cow"_s, cow + QcVec3{-QcPushBodies::maxLead, 0.0, 0.0})).length() < 1e-12);
    }
};

QTEST_GUILESS_MAIN(TestPushBodies)
#include "test_push_bodies.moc"
