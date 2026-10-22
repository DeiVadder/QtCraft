#include "common/core/QcWorldConstants.h"
#include "common/world/QcDayCycle.h"
#include "QcServerTestFixture.h"
#include "server/gameplay/QcServerEffects.h"

#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

// The monster variants of the biomes (decision E-838): the husk does not burn and makes hungry, the
// stray's arrows slow, slowness slows players (on the server and in the client's prediction) and
// creatures, the drowned stays under water and swims up to a player above it. Ticks are stepped by
// hand.
class TestMonsterVariants : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr double noon = QcDayCycle::dayLength * 10.25;
    static constexpr double midnight = QcDayCycle::dayLength * 10.75;
    static constexpr int arena = 16;
    static inline const QString slowness = u"qtcraft:slowness"_s;
    static inline const QString hunger = u"qtcraft:hunger"_s;

    QcServerClient &player() { return *m_server->entities().clients().front(); }
    int type(const char *name) const { return *m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name)); }
    QcCreature *mob(quint32 id) { return m_server->entities().creatures().find(id); }

    // A survival player at `time` on a flat, open arena of grass with food that neither heals nor
    // starves.
    void joinArena(double time)
    {
        join();
        chat(u"/gamemode survival"_s);
        m_server->setWorldTime(time);
        loadAround(m_feet, 2);
        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
                for (int dy = 0; dy <= 12; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        ticks(1);
        player().hunger.restore(10, 0.0);
    }

    quint32 spawn(const char *name, QcBlockPos at)
    {
        return m_server->entities().creatures().spawn(type(name), {at.x + 0.5, double(at.y), at.z + 0.5}, m_server->entities().newId());
    }

    // Metres the player walks forward in `count` ticks of input.
    double walk(int count)
    {
        const QcVec3 start = player().motion.position;
        for (int i = 0; i < count; ++i) {
            QcPlayerInput input;
            input.sequence = quint32(1000 + m_walked++);
            input.forward = 1.0f;
            m_client->connection.send(QcMsg::PlayerInput{input});
            QcTestUtil::pump();
            ticks(1);
        }
        const QcVec3 moved = player().motion.position - start;
        return std::hypot(moved.x, moved.z);
    }

    // Steps ticks until `done` holds, at most `count`; whether it held.
    template<typename Done>
    bool ticksUntil(int count, Done done)
    {
        for (int i = 0; i < count && !done(); ++i) {
            ticks(1);
        }
        return done();
    }

    int m_walked = 0;

private slots:
    void cleanup() { m_client.reset(); }

    void variantsComeFromData()
    {
        const QcMobDefinition &husk = m_content->mobs()[type("husk")];
        const QcMobDefinition &stray = m_content->mobs()[type("stray")];
        const QcMobDefinition &drowned = m_content->mobs()[type("drowned")];
        QVERIFY(husk.isMonster() && !husk.burnsInDaylight && husk.spawnUnderSky && husk.spawnBiomes == QStringList{u"qtcraft:desert"_s});
        QCOMPARE(husk.blowEffects.size(), 1);
        QCOMPARE(husk.blowEffects.first().name, hunger);
        QVERIFY(stray.isMonster() && stray.burnsInDaylight && stray.spawnUnderSky && stray.spawnBiomes == QStringList{u"qtcraft:tundra"_s});
        QCOMPARE(stray.archer.projectile, u"qtcraft:stray_arrow"_s);
        QVERIFY(drowned.isMonster() && drowned.spawnInWater && drowned.abilities.dives && drowned.abilities.swims);
        QCOMPARE(drowned.spawnMaxY, QcWorld::seaLevel - 6);

        const QcEffectDefinition *slow = m_content->effect(slowness);
        const QcEffectDefinition *hungry = m_content->effect(hunger);
        QVERIFY(slow && hungry);
        QCOMPARE(slow->speedFactorAt(1), 0.85);
        QCOMPARE(slow->speedFactorAt(2), 0.7);
        QCOMPARE(slow->speedFactorAt(10), 0.0);
        QCOMPARE(hungry->exhaustion, 0.1);

        // The speed reaches the client with the content; the exhaustion stays on the server.
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        m_content->writeTo(out);
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> received = QcContentRegistry::readFrom(in);
        QVERIFY(received);
        QCOMPARE(received->effect(slowness)->speed, -0.15);
        QCOMPARE(QcStatusEffects::speedFactor(*received, {{slowness, 2, 100}}), 0.7);
    }

    void huskBlowsMakeHungryAndItDoesNotBurn()
    {
        joinArena(noon);
        const quint32 husk = spawn("husk", m_feet.offset(2, 0, 0));
        const quint32 zombie = spawn("zombie", m_feet.offset(-8, 0, 8));
        QVERIFY(ticksUntil(200, [this] { return player().effects.find(hunger) != nullptr; }));
        QVERIFY(player().health < QcServerClient::maxHealth);
        QCOMPARE(mob(husk)->burnTicks, 0);
        QVERIFY(mob(zombie)->burnTicks > 0);
    }

    void hungerExhaustsFood()
    {
        joinArena(noon);
        QVERIFY(QcServerEffects::givePlayer(*m_content, player(), {hunger, 2, 200}));
        const double before = player().hunger.exhaustion();
        idle(100);
        // 0.1 per second and level: 5 s at level II.
        QVERIFY2(std::abs(player().hunger.exhaustion() - before - 1.0) < 0.06, qPrintable(QString::number(player().hunger.exhaustion() - before)));
    }

    void strayArrowsSlow()
    {
        joinArena(midnight);
        spawn("stray", m_feet.offset(9, 0, 3));
        QVERIFY(ticksUntil(200, [this] { return player().effects.find(slowness) != nullptr; }));
        const QcStatusEffect *effect = player().effects.find(slowness);
        QCOMPARE(effect->level, 1);
        QVERIFY(effect->ticksLeft > 580 && effect->ticksLeft <= 600);
    }

    void slownessSlowsPlayersOnTheServer()
    {
        joinArena(noon);
        walk(5);
        const double free = walk(40);
        QVERIFY(QcServerEffects::givePlayer(*m_content, player(), {slowness, 2, 2000}));
        walk(5);
        const double slowed = walk(40);
        // Level II: 70 % of the speed.
        QVERIFY2(std::abs(slowed / free - 0.7) < 0.03, qPrintable(u"%1 / %2"_s.arg(slowed).arg(free)));
    }

    void slownessSlowsCreatures()
    {
        joinArena(noon);
        m_server->setWorldTime(midnight);
        const quint32 fast = spawn("zombie", m_feet.offset(-12, 0, -2));
        const quint32 slow = spawn("zombie", m_feet.offset(-12, 0, 2));
        QVERIFY(QcServerEffects::giveCreature(*m_content, m_server->entities().creatures(), slow, {slowness, 3, 2000}));
        const QcVec3 fastStart = mob(fast)->motion.position;
        const QcVec3 slowStart = mob(slow)->motion.position;
        ticks(30);
        const double fastWay = (mob(fast)->motion.position - fastStart).length();
        const double slowWay = (mob(slow)->motion.position - slowStart).length();
        QVERIFY2(fastWay > 2.0 && slowWay < fastWay * 0.7, qPrintable(u"%1 / %2"_s.arg(slowWay).arg(fastWay)));
    }

    void drownedStaysDownAndSwimsUpToAPlayer()
    {
        joinArena(midnight);
        // A pool of 6 × 6 columns, 7 blocks deep, 6 blocks east of the player.
        const QcBlockPos corner = m_feet.offset(6, -7, -3);
        for (int dx = 0; dx < 6; ++dx) {
            for (int dz = 0; dz < 6; ++dz) {
                m_server->world().setBlock(corner.offset(dx, -1, dz), id("qtcraft:sand"));
                for (int dy = 0; dy < 7; ++dy) {
                    m_server->world().setBlock(corner.offset(dx, dy, dz), id("qtcraft:water"));
                }
            }
        }
        // Creative: nobody to chase, it stays at the bottom.
        chat(u"/gamemode creative"_s);
        const quint32 drowned = spawn("drowned", corner.offset(3, 2, 3));
        ticks(60);
        QVERIFY2(mob(drowned)->motion.position.y < corner.y + 1.0, qPrintable(QString::number(mob(drowned)->motion.position.y)));
        QVERIFY(mob(drowned)->motion.inWater);

        // In survival a player swimming at the surface above draws it up (the server moves players
        // only on input, so he stays there).
        chat(u"/gamemode survival"_s);
        player().motion.position = {corner.x + 3.5, corner.y + 5.6, corner.z + 2.5};
        const double bottom = mob(drowned)->motion.position.y;
        QVERIFY(ticksUntil(400, [this] { return player().health < QcServerClient::maxHealth; }));
        QVERIFY(mob(drowned)->motion.position.y > bottom + 2.0);
    }
};

QTEST_MAIN(TestMonsterVariants)
#include "test_monster_variants.moc"
