// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcCrafting.h"
#include "fixtures/QcServerTestFixture.h"
#include "server/core/QcServerWorld.h"
#include "server/mobbrains/QcFishSwimming.h"
#include "server/pathing/QcSwimSpace.h"
#include "server/spawning/QcNaturalSpawner.h"
#include "worldgen/generators/QcFlatGenerator.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

using namespace Qt::StringLiterals;

// Squids (decision E-974): data, swimming in pulses with routes through the water (QcSwimRoutes,
// decision E-972), tilting along the way, fleeing when hurt, lying still and suffocating on land,
// ink sacs and black dye, spawning only below the sea level. Ticks are stepped by hand.
class TestSquids : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcCreatureKindId squidKind() const { return *m_content->mobKind(u"qtcraft:squid"_s); }
    const QcMobDefinition &squidDefinition() const { return m_content->mobs()[squidKind()]; }
    QcCreature *mob(quint32 id) { return m_server->entities().creatures().find(id); }
    QcMobManager &creatures() { return m_server->entities().creatures(); }
    QcServerClient &player() { return *m_server->entities().clients().front(); }

    // A stone basin of water 17 x 15 and 4 deep beside the player: x 3..19, z -7..7, from the
    // feet's level down.
    void buildPool()
    {
        for (int dx = 2; dx <= 20; ++dx) {
            for (int dz = -8; dz <= 8; ++dz) {
                const bool wall = dx == 2 || dx == 20 || dz == -8 || dz == 8;
                for (int dy = -5; dy <= 4; ++dy) {
                    const bool water = !wall && dy >= -4 && dy <= -1;
                    const QcBlockId block = dy == -5 || (wall && dy <= -1) ? id("qtcraft:stone") : (water ? id("qtcraft:water") : qcAirBlockId);
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), block);
                }
            }
        }
    }

    quint32 spawnSquid(double dx, double dy, double dz)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, m_feet.y + dy, m_feet.z + dz + 0.5};
        return creatures().spawn(squidKind(), at, m_server->entities().newId(), false, false);
    }

    bool centreInWater(const QcCreature &squid) const
    {
        return QcFishSwimming::inWater(m_server->world(), squid.motion.position + QcVec3{0.0, squidDefinition().height * 0.5, 0.0});
    }

    // The squids that a manager with `seed` ticking `world` for two minutes of game time spawns
    // around a player at `height`, with room for five large water creatures and no fish.
    QList<QcCreature> spawnedSquids(QcServerWorld &world, double height, quint64 seed)
    {
        QcMobManager mobs(m_content, seed);
        mobs.spawner().setWaterCaps(0, 5);
        quint32 next = 100;
        const QList<QcMobManager::PlayerView> players{{.entityId = 1, .position = {0.5, height, 0.5}}};
        QList<QcCreature> squids;
        for (int i = 0; i < 20 * 120; ++i) {
            mobs.tick(world, players, 6000.0, 0.05, [&next] { return next++; });
        }
        for (const QcCreature &one : mobs.mobs()) {
            if (one.type == squidKind()) {
                squids.append(one);
            }
        }
        return squids;
    }

    static std::unique_ptr<QcServerWorld> waterWorld(const std::shared_ptr<const QcContentRegistry> &content, const QString &layers)
    {
        auto world = std::make_unique<QcServerWorld>(content, std::make_unique<QcFlatGenerator>(content, layers));
        for (int x = -4; x <= 4; ++x) {
            for (int z = -4; z <= 4; ++z) {
                world->loadChunk({x, z});
            }
        }
        return world;
    }

private slots:
    void squidsComeFromData()
    {
        const QcMobDefinition &squid = squidDefinition();
        QCOMPARE(squid.maxHealth, 10.0);
        QCOMPARE(squid.width, 0.8);
        QVERIFY(!squid.hostile && squid.aquatic && squid.pulse);
        QCOMPARE(squid.aquatic->schoolRadius, 0.0);
        QCOMPARE(squid.aquatic->flopSpeed, 0.0);
        QCOMPARE(squid.ink, u"qtcraft:squid_ink"_s);
        QCOMPARE(std::ranges::count_if(squid.parts, &QcMobPart::tentacle), 8);
        QCOMPARE(squid.spawnMaxY, QcWorld::seaLevel);
        QVERIFY(squid.catchWith.isEmpty());
        // One to three ink sacs, chosen evenly.
        QCOMPARE(squid.drops.size(), 1);
        QCOMPARE(squid.drops.first().item, u"qtcraft:ink_sac"_s);
        QCOMPARE(squid.drops.first().count, 1);
        QCOMPARE(squid.drops.first().maxCount, 3);

        // An ink sac makes black dye, which colours what black dye colours.
        const QcRecipeDefinition dye = m_content->recipes().at(*m_content->recipeIndex(u"qtcraft:black_dye_from_ink_sac"_s));
        QCOMPARE(dye.resultItem, u"qtcraft:black_dye"_s);
        QCOMPARE(QcCrafting::requirements(dye), (QList<std::pair<QString, int>>{{u"qtcraft:ink_sac"_s, 1}}));
        const QcRecipeDefinition wool = m_content->recipes().at(*m_content->recipeIndex(u"qtcraft:black_wool"_s));
        QVERIFY(QcCrafting::requirements(wool).contains(std::pair(u"qtcraft:black_dye"_s, 1)));
    }

    // A squid pushes, glides and pushes again, keeps its centre in the water all the time, comes
    // around the basin and tilts its top along its way.
    void squidsSwimInPulsesInTheWater()
    {
        join();
        buildPool();
        const QList<quint32> squids{spawnSquid(6.0, -3.0, -4.0), spawnSquid(12.0, -3.0, 0.0), spawnSquid(16.0, -3.0, 4.0)};
        const QcVec3 start = mob(squids.first())->motion.position;
        double fastest = 0.0;
        double slowest = 99.0;
        double mostTilt = 0.0;
        double farthest = 0.0;
        int pushes = 0;
        double before = 0.0;
        for (int i = 0; i < 1200; ++i) {
            ticks(1);
            for (const quint32 id : squids) {
                QVERIFY2(mob(id) && centreInWater(*mob(id)), qPrintable(u"tick %1"_s.arg(i)));
            }
            const QcCreature &first = *mob(squids.first());
            const double speed = first.motion.velocity.length();
            // A push: the speed rises by a good step in one tick.
            pushes += speed > before + 0.15 ? 1 : 0;
            before = speed;
            fastest = std::max(fastest, speed);
            slowest = i > 100 ? std::min(slowest, speed) : slowest;
            mostTilt = std::max(mostTilt, static_cast<double>(first.tilt));
            farthest = std::max(farthest, (first.motion.position - start).length());
        }
        QVERIFY2(fastest > 1.0 && fastest < squidDefinition().pulse->speed + 0.2, qPrintable(QString::number(fastest)));
        QVERIFY2(slowest < 0.6, qPrintable(QString::number(slowest)));
        QVERIFY2(pushes >= 5, qPrintable(QString::number(pushes)));
        QVERIFY2(mostTilt > 45.0, qPrintable(QString::number(mostTilt)));
        QVERIFY2(farthest > 3.0, qPrintable(QString::number(farthest)));
        QVERIFY(std::ranges::all_of(squids, [&](quint32 id) { return mob(id)->dryTicks == 0; }));
    }

    // Hit from the west, it darts east in quick pushes; it never strikes back.
    void aHurtSquidFleesFromTheBlow()
    {
        join();
        buildPool();
        const quint32 squid = spawnSquid(11.0, -3.0, 0.0);
        ticks(2);
        const QcVec3 before = mob(squid)->motion.position;
        const double health = player().health;
        QVERIFY(!creatures().damage({.creature = squid, .damage = 1.0, .from = before - QcVec3{2.0, 0.0, 0.0}}));
        ticks(1);
        QVERIFY(mob(squid)->fleeTimer > 0.0);
        for (int i = 0; i < 60; ++i) {
            ticks(1);
            QVERIFY(centreInWater(*mob(squid)));
        }
        const QcVec3 after = mob(squid)->motion.position;
        QVERIFY2(after.x - before.x > 2.0, qPrintable(u"%1 -> %2"_s.arg(before.x).arg(after.x)));
        QCOMPARE(player().health, health);
    }

    // Killed by the player, a squid drops one to three ink sacs, each count about as often.
    void squidsDropOneToThreeInkSacs()
    {
        join();
        buildPool();
        const QcItemId sac = item("qtcraft:ink_sac");
        QHash<int, int> counts;
        constexpr int kills = 90;
        for (int i = 0; i < kills; ++i) {
            const quint32 squid = spawnSquid(3.0, -1.0, 0.0);
            mob(squid)->health = 0.5;
            const int before = countOf(sac) + droppedCount(sac);
            send(QcMsg::AttackEntity{squid});
            ticks(20);
            QVERIFY2(!mob(squid), qPrintable(u"squid %1 lives"_s.arg(i)));
            counts[countOf(sac) + droppedCount(sac) - before] += 1;
        }
        QCOMPARE(counts.size(), 3);
        for (int count = 1; count <= 3; ++count) {
            // A third each: 30 of 90, within about three standard deviations (4.5).
            QVERIFY2(counts.value(count) >= 17 && counts.value(count) <= 43, qPrintable(u"%1 sacs %2 times"_s.arg(count).arg(counts.value(count))));
        }
    }

    // A squid or a cod without a mind (noai) holds its place in the water: a push dies away, the
    // squid rights itself and neither swims anywhere.
    void waterCreaturesWithoutAMindHoldTheirPlace()
    {
        join();
        buildPool();
        const quint32 squid = spawnSquid(11.0, -3.0, 0.0);
        const QcCreatureKindId codKind = *m_content->mobKind(u"qtcraft:cod"_s);
        const quint32 cod = creatures().spawn(codKind, {m_feet.x + 6.5, m_feet.y - 3.0, m_feet.z + 0.5}, m_server->entities().newId(), false, false);
        for (const quint32 id : {squid, cod}) {
            mob(id)->noAi = true;
            mob(id)->motion.velocity = {1.0, 0.0, 0.0};
        }
        mob(squid)->tilt = 70.0f;
        ticks(100);
        const QcVec3 squidAt = mob(squid)->motion.position;
        const QcVec3 codAt = mob(cod)->motion.position;
        ticks(200);
        for (const auto &[id, at] : {std::pair{squid, squidAt}, std::pair{cod, codAt}}) {
            QVERIFY2((mob(id)->motion.position - at).length() < 0.05, qPrintable(u"%1 moved %2"_s.arg(id).arg((mob(id)->motion.position - at).length())));
            QVERIFY(mob(id)->motion.velocity.length() < 0.05);
        }
        QVERIFY2(mob(squid)->tilt < 10.0f, qPrintable(QString::number(mob(squid)->tilt)));
    }

    // A blow strikes a squid (its clients leave ink, EntityMove::struck); suffocating on land
    // hurts one without a strike. Both flash (EntityMove::hurt).
    void onlyBlowsStrikeASquid()
    {
        join();
        buildPool();
        const quint32 swimming = spawnSquid(11.0, -3.0, 0.0);
        const quint32 stranded = spawnSquid(-3.0, 0.0, 2.0);
        ticks(2);
        QVERIFY(!creatures().damage({.creature = swimming, .damage = 1.0, .from = mob(swimming)->motion.position - QcVec3{2.0, 0.0, 0.0}}));
        const int air = static_cast<int>(squidDefinition().aquatic->airSeconds * 20.0);
        for (int i = 0; i < air + 40 && !m_client->hurt.contains(stranded); ++i) {
            ticks(1);
        }
        QVERIFY(m_client->hurt.contains(swimming) && m_client->struck.contains(swimming));
        QVERIFY(m_client->hurt.contains(stranded));
        QVERIFY(!m_client->struck.contains(stranded));
    }

    // On land it does not flop: it sinks onto its side, keeps its health for its air and then
    // suffocates with its ink sacs.
    void squidsLieStillAndSuffocateOnLand()
    {
        join();
        const quint32 squid = spawnSquid(2.0, 0.0, 2.0);
        const int air = static_cast<int>(squidDefinition().aquatic->airSeconds * 20.0);
        for (int i = 0; i < air; ++i) {
            ticks(1);
            QVERIFY(mob(squid));
            QVERIFY2(mob(squid)->motion.velocity.y < 0.5, qPrintable(u"tick %1"_s.arg(i)));
        }
        QCOMPARE(mob(squid)->health, squidDefinition().maxHealth);
        QVERIFY2(mob(squid)->tilt > 80.0f, qPrintable(QString::number(mob(squid)->tilt)));
        for (int i = 0; i < 10 * 20 + 2 && mob(squid); ++i) {
            ticks(1);
        }
        QVERIFY(!mob(squid));
        QVERIFY(droppedCount(item("qtcraft:ink_sac")) >= 1);
    }

    // In water below the sea level squids spawn within their own cap (apart from the fish); in a
    // lake above it none.
    void squidsSpawnBelowTheSeaLevel()
    {
        QCOMPARE(squidDefinition().aquatic->cap, QcAquaticDefinition::Cap::Large);
        QCOMPARE(m_content->mobs()[*m_content->mobKind(u"qtcraft:cod"_s)].aquatic->cap, QcAquaticDefinition::Cap::Fish);
        auto low = waterWorld(m_content, u"bedrock,3*stone,6*water"_s);
        auto high = waterWorld(m_content, u"bedrock,70*stone,6*water"_s);
        for (quint64 seed = 1; seed <= 3; ++seed) {
            const QList<QcCreature> squids = spawnedSquids(*low, 12.0, seed);
            QVERIFY2(!squids.isEmpty() && squids.size() <= 5, qPrintable(u"seed %1: %2"_s.arg(seed).arg(squids.size())));
            for (const QcCreature &one : squids) {
                QVERIFY(!one.persistent);
                QVERIFY(QcFishSwimming::inWater(*low, one.motion.position + QcVec3{0.0, 0.4, 0.0}));
            }
            QVERIFY(spawnedSquids(*high, 80.0, seed).isEmpty());
        }
    }
};

QTEST_MAIN(TestSquids)
#include "test_squids.moc"
