#include "QcServerTestFixture.h"

#include "common/physics/QcPhysics.h"

#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// /summon on a server: where summoned creatures appear and how many may be summoned.
class TestCreatureSummons : public QcServerTestFixture
{
    Q_OBJECT

private:
    int mobType(const char *name) const { return *m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name)); }

    qsizetype countOf(const char *name) const
    {
        const int type = mobType(name);
        return std::ranges::count_if(m_server->mobs().mobs(), [type](const QcCreature &mob) { return mob.type == type; });
    }

    // Whether the creature's body (shrunk by `inset`) overlaps a collision box of a block.
    bool insideBlocks(const QcCreature &mob) const
    {
        const QcMobDefinition &definition = m_content->mobs()[mob.type];
        constexpr double inset = 1e-6;
        const QcAabb box = QcPhysics::boxAt(mob.motion.position, definition.width, definition.height);
        const QcAabb body{box.min + QcVec3{inset, inset, inset}, box.max - QcVec3{inset, inset, inset}};
        for (int x = int(std::floor(body.min.x + inset)); x <= int(std::floor(body.max.x - inset)); ++x) {
            // From one cell lower: fences and walls reach 1.5 m up.
            for (int y = int(std::floor(body.min.y + inset)) - 1; y <= int(std::floor(body.max.y - inset)); ++y) {
                for (int z = int(std::floor(body.min.z + inset)); z <= int(std::floor(body.max.z - inset)); ++z) {
                    const QList<QcAabb> boxes = QcPhysics::collisionBoxesAt(m_server->world(), {x, y, z});
                    if (std::ranges::any_of(boxes, [&body](const QcAabb &box) { return box.intersects(body); })) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Persistent pigs that came with the chunks (not summoned) on the stage around the player.
    void addNaturalPigs(int count)
    {
        for (int i = 0; i < count; ++i) {
            const QcVec3 at{m_feet.x - 2.5 + (i % 6) * 0.8, double(m_feet.y), m_feet.z + 1.5 + std::floor(i / 6.0) * 0.1};
            m_server->mobs().spawn(mobType("pig"), at, 50000 + quint32(i), false, true);
        }
    }

    // Fills the area 2 to 4 m ahead of the player (north), 3 m to each side, from dy `low` to `high`
    // (or the rows from `nearZ` to `farZ` metres ahead) with `block`.
    void fillAhead(int low, int high, const char *block, int nearZ = -2, int farZ = -4)
    {
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dz = farZ; dz <= nearZ; ++dz) {
                for (int dy = low; dy <= high; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), id(block));
                }
            }
        }
    }

    // Runs a /summon line and returns the ids of the creatures it added.
    QList<quint32> summon(const QString &line)
    {
        QSet<quint32> before;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            before.insert(mob.id);
        }
        chat(line);
        QList<quint32> added;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            if (!before.contains(mob.id)) {
                added.append(mob.id);
            }
        }
        return added;
    }

    static QString where(const QcCreature &mob)
    {
        return u"%1 %2 %3"_s.arg(mob.motion.position.x).arg(mob.motion.position.y).arg(mob.motion.position.z);
    }

private slots:
    void summonedCreaturesFitIntoTheWorld()
    {
        // A one-block step of stone where the creatures would appear (3 m ahead, the player looks
        // north): they stand on top of it, not inside it, and live on.
        join();
        fillAhead(0, 0, "qtcraft:stone");
        chat(u"/time set day"_s);
        QList<quint32> summoned = summon(u"/summon pig 3"_s);
        summoned += summon(u"/summon zombie 3"_s);
        QCOMPARE(summoned.size(), 6);
        for (const quint32 mobId : std::as_const(summoned)) {
            const QcCreature &mob = *m_server->mobs().find(mobId);
            const bool inside = insideBlocks(mob);
            const double feetY = mob.motion.position.y;
            QVERIFY2(!inside, qPrintable(where(mob)));
            QCOMPARE(feetY, m_feet.y + 1.0);
        }

        // Ten seconds: long enough to suffocate a pig (1 damage every 10 ticks, 10 health).
        idle(200);
        const qsizetype alive = std::ranges::count_if(summoned, [this](quint32 id) { return m_server->mobs().find(id) != nullptr; });
        QCOMPARE(alive, 6);
    }

    void summonedIntoAFenceLineComesOutBesideIt()
    {
        // A fence is 1.5 m high: not on top of it (inside its upper half) but north of it.
        join();
        fillAhead(-1, -1, "qtcraft:grass_block", -2, -6);
        fillAhead(0, 2, "qtcraft:air", -2, -6);
        fillAhead(0, 0, "qtcraft:oak_fence", -3, -3);
        const QList<quint32> summoned = summon(u"/summon pig"_s);
        QCOMPARE(summoned.size(), 1);
        idle(20);

        const QcCreature &pig = *m_server->mobs().find(summoned.constFirst());
        const bool inside = insideBlocks(pig);
        const double feetY = pig.motion.position.y;
        const double fenceNorthFace = m_feet.z - 3 + 0.375;
        QVERIFY2(!inside, qPrintable(where(pig)));
        QCOMPARE(feetY, double(m_feet.y));
        QVERIFY2(pig.motion.position.z < fenceNorthFace, qPrintable(where(pig)));
    }

    void summonedIntoWaterSwims()
    {
        // Water has no collision: the creature appears where it was wished, in the water.
        join();
        fillAhead(-2, 1, "qtcraft:water");
        const QcVec3 wanted = m_client->motion.position + QcVec3{0.0, 0.5, -3.0};
        const QList<quint32> summoned = summon(u"/summon pig"_s);
        QCOMPARE(summoned.size(), 1);
        const QcVec3 placed = m_server->mobs().find(summoned.constFirst())->motion.position;
        QCOMPARE(placed.y, wanted.y);
        QCOMPARE(placed.z, wanted.z);

        idle(20);
        const QcCreature *pig = m_server->mobs().find(summoned.constFirst());
        QVERIFY(pig != nullptr);
        const bool swimming = pig->motion.inWater;
        QVERIFY2(swimming, qPrintable(where(*pig)));
    }

    void summonedIntoLavaAppearsAtThePlayer()
    {
        // Lava has no collision either, but a creature is not summoned into it (decision E-352).
        join();
        fillAhead(-2, 1, "qtcraft:lava");
        const QList<quint32> summoned = summon(u"/summon pig"_s);
        QCOMPARE(summoned.size(), 1);
        const QcVec3 placed = m_server->mobs().find(summoned.constFirst())->motion.position;
        const QcVec3 player = m_client->motion.position;
        QCOMPARE(placed.x, player.x);
        QCOMPARE(placed.y, player.y);
        QCOMPARE(placed.z, player.z);
    }

    void summonLimitCountsOnlySummonedCreatures()
    {
        // 70 animals that came with the chunks do not use up the limit of summoned creatures.
        join();
        addNaturalPigs(70);
        const qsizetype pigsBefore = countOf("pig");
        const qsizetype zombiesBefore = countOf("zombie");
        for (int i = 0; i < 4; ++i) {
            chat(u"/summon pig 16"_s);
        }
        const qsizetype pigsAfter = countOf("pig");
        QCOMPARE(pigsAfter, pigsBefore + 64);

        // The 65th summoned creature is one too many.
        chat(u"/summon zombie"_s);
        const qsizetype zombiesAfter = countOf("zombie");
        QCOMPARE(zombiesAfter, zombiesBefore);
    }

    void summonLimitIsConfigurable()
    {
        QcServerConfig config;
        config.limits.summonLimit = 8;
        join(config);
        const qsizetype before = countOf("slime");
        chat(u"/summon slime 9"_s);
        const qsizetype tooMany = countOf("slime");
        QCOMPARE(tooMany, before);
        chat(u"/summon slime 8"_s);
        const qsizetype allowed = countOf("slime");
        QCOMPARE(allowed, before + 8);
        chat(u"/summon slime"_s);
        const qsizetype full = countOf("slime");
        QCOMPARE(full, before + 8);
    }
};

QTEST_GUILESS_MAIN(TestCreatureSummons)
#include "test_creature_summons.moc"
