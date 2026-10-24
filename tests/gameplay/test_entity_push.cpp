#include "common/physics/QcBodyPush.h"
#include "QcServerTestFixture.h"

#include <QTest>

#include <cmath>
#include <tuple>

using namespace Qt::StringLiterals;

// The soft collision between players and creatures on the server (decision E-882): bodies that
// overlap are pushed apart over a few ticks; a player walking into a villager is slowed and turned
// aside but gets through; a creature without a mind pushes the player and stays; spectators and
// riders of carts are neither pushed nor push, lying villagers do not push; a chasing zombie still
// strikes; a player in a dense crowd is pushed no faster than the push speed.
class TestEntityPush : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr float east = 90.0f;

    const QcServerClient &player() const { return *m_server->entities().clients().front(); }

    int type(const char *name) const
    {
        const std::optional<int> found = m_content->mobIndex(QString::fromLatin1(name));
        if (!found) {
            QcTestUtil::failCase(u"Test uses the creature %1, which the content does not have"_s.arg(QString::fromLatin1(name)));
            return 0;
        }
        return *found;
    }

    // A creature of `name` at `offset` from the player's feet; without a mind for `noAi`.
    quint32 spawn(const char *name, const QcVec3 &offset, bool noAi)
    {
        const quint32 id = m_server->mobs().spawn(type(name), player().motion.position + offset, m_server->entities().newId(), false, true);
        if (QcCreature *mob = m_server->mobs().find(id)) {
            mob->noAi = noAi;
        }
        return id;
    }

    // A creature of `name` with a mind at `offset` that stands where it is: no place to wander to
    // for a long time, so only a push moves it.
    quint32 spawnStanding(const char *name, const QcVec3 &offset)
    {
        const quint32 id = spawn(name, offset, false);
        if (QcCreature *mob = m_server->mobs().find(id)) {
            mob->wanderTimer = 1e9;
            mob->wanderDirection = {};
            mob->path.clear();
        }
        return id;
    }

    QcVec3 creatureAt(quint32 id) const
    {
        const QcCreature *mob = m_server->mobs().find(id);
        return mob ? mob->motion.position : QcVec3{};
    }

    static double across(const QcVec3 &a, const QcVec3 &b) { return std::hypot(a.x - b.x, a.z - b.z); }

    // Joins with a flat floor from 3 blocks west to 14 east of the player and nothing on it: no
    // animals, no spawning.
    void joinOnAField()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        for (int dx = -3; dx <= 14; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:stone"));
                for (int dy = 0; dy <= 3; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        m_server->mobs().removeAll();
        idle(5);
    }

    // Sends `count` inputs of walking towards `yaw`, one per tick.
    void walk(float yaw, int count)
    {
        for (int i = 0; i < count; ++i) {
            QcPlayerInput input;
            input.sequence = ++m_sequence;
            input.yaw = yaw;
            input.forward = 1.0f;
            m_client->connection.send(QcMsg::PlayerInput{input});
            QcTestUtil::pump();
            ticks(1);
        }
    }

private slots:
    void aCreatureAndAPlayerOverlappingArePushedApart()
    {
        joinOnAField();
        const QcVec3 start = player().motion.position;
        const quint32 cow = spawn("qtcraft:cow", {0.15, 0.0, 0.05}, false);
        const QcVec3 cowStart = creatureAt(cow);
        const double reach = QcPhysics::playerWidth * 0.5 + m_content->mobs()[type("qtcraft:cow")].width * 0.5;
        QVERIFY(across(start, cowStart) < reach * 0.3);

        idle(20);
        const QcVec3 now = player().motion.position;
        const QcVec3 cowNow = creatureAt(cow);
        QVERIFY2(across(now, cowNow) > reach * 0.9, qPrintable(QString::number(across(now, cowNow))));
        // Both gave way, each to its own side of the line between them.
        const QcVec3 line = QcVec3{cowStart.x - start.x, 0.0, cowStart.z - start.z}.normalized();
        QVERIFY2((now - start).dot(line) < -0.05, qPrintable(QString::number((now - start).dot(line))));
        QVERIFY2((cowNow - cowStart).dot(line) > 0.05, qPrintable(QString::number((cowNow - cowStart).dot(line))));
    }

    void aCreatureWithoutAMindPushesAndStays()
    {
        joinOnAField();
        chat(u"/gamemode survival"_s);
        idle(1);
        const QcVec3 start = player().motion.position;
        const double exhaustion = player().hunger.exhaustion();
        const quint32 cow = spawn("qtcraft:cow", {0.0, 0.0, 0.0}, true);
        const QcVec3 cowStart = creatureAt(cow);

        idle(20);
        // Being pushed costs no food.
        QCOMPARE(player().hunger.exhaustion(), exhaustion);
        // On the same spot the player goes towards +x (QcBodyPush::playerParting) out of its body.
        const double reach = QcPhysics::playerWidth * 0.5 + m_content->mobs()[type("qtcraft:cow")].width * 0.5;
        QVERIFY2(player().motion.position.x - start.x > reach * 0.9, qPrintable(QString::number(player().motion.position.x - start.x)));
        QCOMPARE(creatureAt(cow).x, cowStart.x);
        QCOMPARE(creatureAt(cow).z, cowStart.z);
    }

    void walkingIntoAVillagerSlowsAndTurnsButGetsThrough()
    {
        joinOnAField();
        const QcVec3 start = player().motion.position;
        // A villager without a mind three blocks ahead, a little to the side of the way.
        const quint32 villager = spawn("qtcraft:villager", {3.0, 0.0, 0.2}, true);
        const QcVec3 villagerAt = creatureAt(villager);

        // The same walk without anybody, in the server's world.
        QcEntityMotion free = player().motion;
        QcPlayerInput input;
        input.yaw = east;
        input.forward = 1.0f;
        constexpr int steps = 40;
        for (int i = 0; i < steps; ++i) {
            free.stepPlayer(input, m_server->world(), QcPhysics::tickDuration, QcMovement::Walk);
        }

        double closest = 1e9;
        double slowest = 1e9;
        QcVec3 before = player().motion.position;
        for (int i = 0; i < steps; ++i) {
            walk(east, 1);
            const QcVec3 now = player().motion.position;
            closest = std::min(closest, across(now, villagerAt));
            slowest = std::min(slowest, (now.x - before.x) / QcPhysics::tickDuration);
            before = now;
        }
        const QcVec3 end = player().motion.position;
        // Slowed and turned aside (the villager stands a little to the left of the way, so to the
        // right), but past it and walking on: not stuck.
        QVERIFY2(end.x < free.position.x - 0.05, qPrintable(u"%1 %2"_s.arg(end.x).arg(free.position.x)));
        QVERIFY2(end.z < start.z - 0.05, qPrintable(QString::number(end.z - start.z)));
        QVERIFY2(end.x > villagerAt.x + 1.5, qPrintable(QString::number(end.x - villagerAt.x)));
        QVERIFY2(slowest > 0.0, qPrintable(QString::number(slowest)));
        QVERIFY2(slowest < QcPhysics::walkSpeed - 0.5, qPrintable(QString::number(slowest)));
        // It came close: bodies are soft, no wall.
        QVERIFY2(closest < 0.6, qPrintable(QString::number(closest)));
        QCOMPARE(creatureAt(villager).x, villagerAt.x);
    }

    void aStandingCreatureWithAMindIsPushedAside()
    {
        // The counterpart of the cases below: a player pushes such a pig out of his body.
        joinOnAField();
        const quint32 pig = spawnStanding("qtcraft:pig", {0.0, 0.0, 0.1});
        const QcVec3 pigStart = creatureAt(pig);
        idle(20);
        QVERIFY2(across(creatureAt(pig), pigStart) > 0.1, qPrintable(QString::number(across(creatureAt(pig), pigStart))));
    }

    void aSpectatorIsNeitherPushedNorPushes()
    {
        joinOnAField();
        chat(u"/gamemode spectator"_s);
        idle(2);
        const QcVec3 start = player().motion.position;
        // A pig with a mind that stands in him: it would push a body, and a body would move it (see
        // above).
        const quint32 pig = spawnStanding("qtcraft:pig", {0.0, 0.0, 0.1});
        const QcVec3 pigStart = creatureAt(pig);
        idle(20);
        QCOMPARE(player().motion.position, start);
        QVERIFY2(across(creatureAt(pig), pigStart) < 1e-9, qPrintable(QString::number(across(creatureAt(pig), pigStart))));
    }

    void aLyingVillagerDoesNotPush()
    {
        joinOnAField();
        const QcVec3 start = player().motion.position;
        // In bed (decision E-411): the villager lies where the player stands.
        const quint32 villager = spawn("qtcraft:villager", {0.1, 0.0, 0.0}, true);
        m_server->mobs().find(villager)->pose = QcPose::Lying;
        idle(20);
        QVERIFY2(across(player().motion.position, start) < 1e-9, qPrintable(QString::number(across(player().motion.position, start))));

        // Standing up, it pushes.
        m_server->mobs().find(villager)->pose = QcPose::Standing;
        idle(20);
        QVERIFY2(across(player().motion.position, start) > 0.1, qPrintable(QString::number(across(player().motion.position, start))));
    }

    void aSeatedPlayerIsNeitherPushedNorPushes()
    {
        joinOnAField();
        // A cart on the field east of the player, ridden.
        QcMinecart standing = m_server->carts().fromItem(u"qtcraft:minecart"_s);
        standing.motion.position = player().motion.position + QcVec3{2.0, 0.0, 0.0};
        const quint32 cart = m_server->systems().minecarts.spawn(standing)->id;
        ticks(2);
        send(QcMsg::UseEntity{cart});
        idle(2);
        QCOMPARE(m_client->vehicle, cart);

        // A pig with a mind standing in the rider: he does not push it, and stays in his seat
        // (the pig's body may push the cart, decision E-382).
        const quint32 pig = spawnStanding("qtcraft:pig", {0.1, 0.0, 0.0});
        const QcVec3 pigStart = creatureAt(pig);
        idle(20);
        QVERIFY2(across(creatureAt(pig), pigStart) < 1e-9, qPrintable(QString::number(across(creatureAt(pig), pigStart))));
        const QcVec3 seat = m_server->carts().find(cart)->motion.position;
        QVERIFY2(across(player().motion.position, seat) < 1e-6, qPrintable(QString::number(across(player().motion.position, seat))));
        QCOMPARE(m_client->vehicle, cart);
    }

    void aChasingZombieInThePlayerStillStrikesAtItsRate()
    {
        joinOnAField();
        chat(u"/gamemode survival"_s);
        chat(u"/time set night"_s);
        // A zombie in the player's body that has seen him: it presses in rather than going around
        // him, and strikes once per attackCooldown.
        const quint32 zombie = spawn("qtcraft:zombie", {0.3, 0.0, 0.0}, false);
        m_server->mobs().find(zombie)->chasing = player().id;
        m_server->mobs().find(zombie)->targetMemory = 1e9;
        constexpr int ticks = 90;
        int hits = 0;
        double health = player().health;
        for (int i = 0; i < ticks; ++i) {
            idle(1);
            hits += player().health < health ? 1 : 0;
            health = player().health;
        }
        const double cooldown = m_content->mobs()[type("qtcraft:zombie")].attackCooldown;
        const int expected = static_cast<int>(ticks * QcPhysics::tickDuration / cooldown);
        QVERIFY2(hits >= expected - 1, qPrintable(u"%1 hits, %2 expected"_s.arg(hits).arg(expected)));
    }

    void aCrowdPushesNoFasterThanThePushSpeed()
    {
        joinOnAField();
        // Sixty pigs packed around the player, none of them hurt by the crowd.
        chat(u"/gamerule maxEntityCramming 0"_s);
        for (int i = 0; i < 60; ++i) {
            const double angle = i * 0.7;
            const double distance = 0.05 + 0.01 * i;
            std::ignore = spawn("qtcraft:pig", {distance * std::cos(angle), 0.0, distance * std::sin(angle)}, true);
        }
        QcVec3 before = player().motion.position;
        double fastest = 0.0;
        for (int i = 0; i < 20; ++i) {
            idle(1);
            const QcVec3 now = player().motion.position;
            fastest = std::max(fastest, across(now, before) / QcPhysics::tickDuration);
            before = now;
        }
        QVERIFY2(fastest > 0.5, qPrintable(QString::number(fastest)));
        QVERIFY2(fastest <= QcBodyPush::playerSpeed + 1e-9, qPrintable(QString::number(fastest)));
    }
};

QTEST_GUILESS_MAIN(TestEntityPush)
#include "test_entity_push.moc"
