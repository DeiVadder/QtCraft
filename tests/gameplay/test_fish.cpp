#include "QcServerTestFixture.h"
#include "server/mobs/QcFishSwimming.h"
#include "server/mobs/QcMobCrowd.h"
#include "server/spawning/QcNaturalSpawner.h"

#include <QElapsedTimer>
#include "worldgen/generators/QcFlatGenerator.h"

using namespace Qt::StringLiterals;

// Fish (decision E-502) and fish buckets (decision E-501): they swim only in water, keep together in
// schools, flop and suffocate on land, are caught with a water bucket and released (then kept with
// the world), and spawn within their own cap. Ticks are stepped by hand.
class TestFish : public QcServerTestFixture
{
    Q_OBJECT

private:
    int type(const char *name) const { return *m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name)); }
    const QcMobDefinition &kind(const char *name) const { return m_content->mobs()[type(name)]; }
    QcCreature *mob(quint32 id) { return m_server->entities().creatures().find(id); }
    QcMobManager &creatures() { return m_server->entities().creatures(); }

    // A stone basin of water (or `liquid`) 17 x 15 and 4 deep beside the player: x 3..19, z -7..7,
    // from the feet's level down.
    void buildPool(const char *liquid = "qtcraft:water")
    {
        for (int dx = 2; dx <= 20; ++dx) {
            for (int dz = -8; dz <= 8; ++dz) {
                const bool wall = dx == 2 || dx == 20 || dz == -8 || dz == 8;
                for (int dy = -5; dy <= 4; ++dy) {
                    const QcBlockPos at = m_feet.offset(dx, dy, dz);
                    const bool water = !wall && dy >= -4 && dy <= -1;
                    const QcBlockId block = dy == -5 || (wall && dy <= -1) ? id("qtcraft:stone") : (water ? id(liquid) : qcAirBlockId);
                    m_server->world().setBlock(at, block);
                }
            }
        }
    }

    quint32 spawnFish(const char *name, double dx, double dy, double dz, bool persistent = false)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, m_feet.y + dy, m_feet.z + dz + 0.5};
        return creatures().spawn(type(name), at, m_server->entities().newId(), false, persistent);
    }

    bool inWater(const QcCreature &fish) const
    {
        const QcVec3 centre = fish.motion.position + QcVec3{0.0, m_content->mobs()[fish.type].height * 0.5, 0.0};
        return QcFishSwimming::inWater(m_server->world(), centre);
    }

    // Mean distance of the creatures to their centre.
    static double spread(const QList<QcVec3> &positions)
    {
        QcVec3 centre;
        for (const QcVec3 &p : positions) {
            centre += p;
        }
        centre = centre * (1.0 / double(positions.size()));
        double sum = 0.0;
        for (const QcVec3 &p : positions) {
            sum += (p - centre).length();
        }
        return sum / double(positions.size());
    }

    void hold(const char *name)
    {
        const int slot = slotOf(item(name));
        QVERIFY2(slot >= 0 && slot < 9, name);
        send(QcMsg::SelectHotbarSlot{slot});
    }

private slots:
    void fishComeFromData()
    {
        for (const char *name : {"cod", "salmon"}) {
            const QcMobDefinition &fish = kind(name);
            QVERIFY(fish.aquatic);
            QCOMPARE(fish.aquatic->airSeconds, 15.0);
            QCOMPARE(fish.catchWith, u"qtcraft:water_bucket"_s);
            QCOMPARE(fish.catchItem, u"qtcraft:"_s + QString::fromLatin1(name) + u"_bucket"_s);
            const QcItemDefinition &bucket = m_content->item(item(qPrintable(fish.catchItem)));
            QCOMPARE(bucket.releasesMob, fish.name);
            QCOMPARE(bucket.placesBlock, u"qtcraft:water"_s);
            QCOMPARE(bucket.emptiesTo, u"qtcraft:bucket"_s);
        }
        QVERIFY(std::ranges::find(kind("salmon").drops, u"qtcraft:raw_salmon"_s, &QcItemDrop::item) != kind("salmon").drops.end());
        QVERIFY(kind("salmon").spawnBiomes.contains(u"qtcraft:tundra"_s));
        QVERIFY(std::ranges::any_of(m_content->smeltingRecipes(),
                                    [](const QcSmeltingRecipe &recipe) { return recipe.inputItem == u"qtcraft:raw_salmon"_s; }));
        QCOMPARE(QcServerLimits{}.waterCreaturesPerPlayer, 5);
    }

    void schoolingSteersTowardsAlongAndApart()
    {
        QcAquaticDefinition aquatic;
        aquatic.alignment = 0.0;
        aquatic.separation = 0.0;
        QcCreature a;
        a.motion.position = {3.0, 0.0, 0.0};
        QcCreature b;
        b.motion.position = {3.0, 0.0, 2.0};
        // Cohesion alone: towards the centre of the others.
        const QcVec3 towards = QcFishSwimming::schooling({0.0, 0.0, 1.0}, {&a, &b}, aquatic);
        QVERIFY(towards.x > 0.0 && std::abs(towards.z) < 1e-9);

        // Alignment alone: along their mean heading.
        aquatic.cohesion = 0.0;
        aquatic.alignment = 1.0;
        a.motion.velocity = {0.0, 0.0, -1.0};
        b.motion.velocity = {0.0, 0.0, -1.0};
        const QcVec3 along = QcFishSwimming::schooling({0.0, 0.0, 1.0}, {&a, &b}, aquatic);
        QVERIFY(along.z < -0.99 && std::abs(along.x) < 1e-9);

        // Separation alone: away from one too close, nothing from those farther.
        aquatic.alignment = 0.0;
        aquatic.separation = 1.0;
        QcCreature close;
        close.motion.position = {0.3, 0.0, 0.0};
        const QcVec3 apart = QcFishSwimming::schooling({0.0, 0.0, 0.0}, {&close, &a}, aquatic);
        QVERIFY(apart.x < 0.0);
        QVERIFY(QcFishSwimming::schooling({0.0, 0.0, 0.0}, {}, aquatic).length() == 0.0);
    }

    void fishStayInTheWaterAndSchool()
    {
        join();
        buildPool();
        QList<quint32> school;
        // Spread within one school radius.
        for (const auto &[dx, dz] : {std::pair{9, -3}, {15, -3}, {9, 3}, {15, 3}, {12, 0}, {10, 0}, {14, 0}, {12, 3}}) {
            school.append(spawnFish("cod", dx, -3.0, dz));
        }
        const auto positions = [&] {
            QList<QcVec3> all;
            for (const quint32 id : std::as_const(school)) {
                all.append(mob(id)->motion.position);
            }
            return all;
        };
        const double before = spread(positions());
        for (int i = 0; i < 600; ++i) {
            ticks(1);
            for (const quint32 id : std::as_const(school)) {
                QVERIFY2(mob(id) && inWater(*mob(id)), qPrintable(u"tick %1"_s.arg(i)));
            }
        }
        const double after = spread(positions());
        // Together, but not on top of each other.
        QVERIFY2(after < before * 0.6, qPrintable(u"%1 -> %2"_s.arg(before).arg(after)));
        QVERIFY2(after > 0.3, qPrintable(QString::number(after)));
        // They moved, and no fish is out of breath.
        QVERIFY(std::ranges::all_of(school, [&](quint32 id) { return mob(id)->dryTicks == 0; }));
    }

    void fishFlopAndSuffocateOnLand()
    {
        join();
        // Walls keep it on the stage, so it cannot flop into other water.
        const quint32 fish = spawnFish("salmon", 2.0, 0.0, 2.0);
        bool flopped = false;
        const int air = int(kind("salmon").aquatic->airSeconds * 20.0);
        for (int i = 0; i < air; ++i) {
            ticks(1);
            QVERIFY(mob(fish));
            flopped = flopped || mob(fish)->motion.velocity.y > 1.0;
        }
        QVERIFY(flopped);
        QCOMPARE(mob(fish)->health, kind("salmon").maxHealth);
        // Then one damage a second: dead three seconds later, with its drop.
        for (int i = 0; i < 3 * 20 + 2 && mob(fish); ++i) {
            ticks(1);
        }
        QVERIFY(!mob(fish));
        QVERIFY(droppedCount(item("qtcraft:raw_salmon")) == 1);
    }

    void aFishBackInTheWaterGetsItsBreathBack()
    {
        join();
        buildPool();
        const quint32 fish = spawnFish("cod", 12.0, -3.0, 0.0);
        mob(fish)->dryTicks = 200;
        ticks(1);
        QCOMPARE(mob(fish)->dryTicks, 0);
    }

    void caughtWithABucketAndReleasedForGood()
    {
        join();
        chat(u"/gamemode survival"_s);
        buildPool();
        // Over the pool's wall, within reach.
        const quint32 fish = spawnFish("cod", 4.0, -1.0, 0.0);
        ticks(1);
        chat(u"/give water_bucket 1"_s);
        hold("qtcraft:water_bucket");
        send(QcMsg::UseEntity{fish});
        QVERIFY(!mob(fish));
        QCOMPARE(countOf(item("qtcraft:cod_bucket")), 1);
        QCOMPARE(countOf(item("qtcraft:water_bucket")), 0);

        // Poured onto the stage: water and a cod that is kept with the world.
        const qsizetype before = std::ranges::count(creatures().mobs(), type("cod"), &QcCreature::type);
        hold("qtcraft:cod_bucket");
        const QcBlockPos ground = m_feet.offset(-2, -1, 0);
        send(QcMsg::UseItem{ground, QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(ground.offset(0, 1, 0)), std::optional<QcBlockId>(id("qtcraft:water")));
        QCOMPARE(countOf(item("qtcraft:bucket")), 1);
        QCOMPARE(std::ranges::count(creatures().mobs(), type("cod"), &QcCreature::type), before + 1);
        const auto released = std::ranges::find_if(creatures().mobs(), [&](const QcCreature &one) {
            return one.type == type("cod") && (one.motion.position - QcVec3{ground.x + 0.5, ground.y + 1.2, ground.z + 0.5}).length() < 1.5;
        });
        QVERIFY(released != creatures().mobs().end());
        QVERIFY(released->persistent);
        QVERIFY(released->uid != 0);
    }

    // Debug-relative measurement (decision E-502, docs/Performance-Verlauf.md "52.4"): the same world
    // without creatures, with 60 fish in the pool, then with 20 cows on the stage as well, ticks
    // stepped by hand; the fish and the cows are reported apart.
    void measureSixtyFishAndTwentyCows()
    {
        join();
        buildPool();
        const auto meanTickMs = [this](int count) {
            QElapsedTimer timer;
            timer.start();
            ticks(count);
            return double(timer.nsecsElapsed()) / 1e6 / count;
        };
        ticks(20);
        const double alone = meanTickMs(200);
        for (int i = 0; i < 60; ++i) {
            const int layer = i / 15;
            spawnFish(i % 2 == 0 ? "cod" : "salmon", 3.0 + i % 15, -3.0 + layer * 0.5, -6.0 + (i * 7) % 13, true);
        }
        ticks(20);
        const double withFish = meanTickMs(200);
        for (int i = 0; i < 20; ++i) {
            const int row = i / 5;
            const QcVec3 at{m_feet.x - 2.5 + i % 5, double(m_feet.y), m_feet.z - 2.5 + row};
            creatures().spawn(*m_content->mobIndex(u"qtcraft:cow"_s), at, m_server->entities().newId(), false, true);
        }
        ticks(20);
        const double withCows = meanTickMs(200);
        qInfo().noquote() << u"MEASURE tick alone %1 ms, with 60 fish %2 ms (%3 us per fish), with 20 cows more %4 ms (%5 us per cow)"_s
                                 .arg(alone, 0, 'f', 3)
                                 .arg(withFish, 0, 'f', 3)
                                 .arg((withFish - alone) * 1000.0 / 60.0, 0, 'f', 1)
                                 .arg(withCows, 0, 'f', 3)
                                 .arg((withCows - withFish) * 1000.0 / 20.0, 0, 'f', 1);
        const qsizetype fish = std::ranges::count_if(creatures().mobs(), [this](const QcCreature &one) { return m_content->mobs()[one.type].aquatic.has_value(); });
        QCOMPARE(fish, 60);
    }

    void lavaIsNoWaterForFish()
    {
        join();
        buildPool("qtcraft:lava");
        QVERIFY(!QcFishSwimming::inWater(m_server->world(), QcVec3{m_feet.x + 12.5, m_feet.y - 2.5, m_feet.z + 0.5}));
        const quint32 fish = spawnFish("cod", 12.0, -3.0, 0.0);
        ticks(2);
        // Out of water: it counts dry ticks (if the lava has not killed it yet).
        QVERIFY(!mob(fish) || mob(fish)->dryTicks > 0);
    }

    void noFishSpawnInLava()
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcFlatGenerator>(m_content, u"bedrock,3*stone,6*lava"_s));
        for (int x = -4; x <= 4; ++x) {
            for (int z = -4; z <= 4; ++z) {
                world->loadChunk({x, z});
            }
        }
        QcMobManager mobs(m_content, 7);
        mobs.spawner().setWaterCap(3);
        quint32 next = 100;
        const QList<QcMobManager::PlayerView> players{{.entityId = 1, .position = {0.5, 12.0, 0.5}}};
        for (int i = 0; i < 20 * 60; ++i) {
            mobs.tick(*world, players, 6000.0, 0.05, [&next] { return next++; });
        }
        QVERIFY(std::ranges::none_of(mobs.mobs(), [this](const QcCreature &one) { return m_content->mobs()[one.type].aquatic.has_value(); }));
    }

    void naturalFishDespawnFarFromPlayers()
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcFlatGenerator>(m_content, u"bedrock,3*stone,6*water"_s));
        for (int x = -1; x <= 1; ++x) {
            for (int z = -1; z <= 1; ++z) {
                world->loadChunk({x, z});
            }
        }
        QcMobManager mobs(m_content, 7);
        mobs.spawner().setWaterCap(0);
        const quint32 wild = mobs.spawn(type("cod"), {8.5, 6.0, 8.5}, 10);
        const quint32 released = mobs.spawn(type("cod"), {9.5, 6.0, 8.5}, 11, false, true);
        // The only player is far beyond the despawn distance.
        const QList<QcMobManager::PlayerView> players{{.entityId = 1, .position = {QcMobManager::despawnDistance * 2.0, 12.0, 0.5}}};
        quint32 next = 100;
        for (int i = 0; i < 20 && mobs.find(wild); ++i) {
            mobs.tick(*world, players, 6000.0, 0.05, [&next] { return next++; });
        }
        QVERIFY(!mobs.find(wild));
        QVERIFY(mobs.find(released));
    }

    void waterCreaturesTakeNoPartInCrowds()
    {
        QList<QcCreature> crowd;
        for (int i = 0; i < 40; ++i) {
            QcCreature fish;
            fish.id = quint32(i + 1);
            fish.type = type("cod");
            fish.motion.position = {0.5, 0.5, 0.5};
            crowd.append(fish);
        }
        QcMobCrowd crowds;
        crowds.setThresholds(8, 24);
        crowds.build(crowd, m_content->mobs());
        for (qsizetype i = 0; i < crowd.size(); ++i) {
            QCOMPARE(crowds.countAt(i), 0);
            QCOMPARE(crowds.tierAt(i), QcMobCrowd::Tier::Full);
        }
        QVERIFY(crowds.crammed(24).isEmpty());
    }

    void aDenseSchoolKeepsNoDirection()
    {
        join();
        buildPool();
        // 60 cod packed in the middle of the pool (x 3..19, z -7..7, y -4..-1).
        QList<quint32> school;
        for (int i = 0; i < 60; ++i) {
            const int layer = i / 25;
            const int row = (i / 5) % 5;
            school.append(spawnFish("cod", 9.0 + i % 5, -3.5 + layer * 0.8, -2.0 + row, true));
        }
        QcVec3 start;
        for (const quint32 id : std::as_const(school)) {
            start += mob(id)->motion.position * (1.0 / 60.0);
        }

        // Neighbours taken in grid order would pull every fish to the low corner; the nearest do not.
        QcVec3 sum;
        constexpr int rounds = 600;
        for (int i = 0; i < rounds; ++i) {
            ticks(1);
            QcVec3 centre;
            for (const quint32 id : std::as_const(school)) {
                centre += mob(id)->motion.position * (1.0 / 60.0);
            }
            sum += centre - start;
        }
        const QcVec3 drift = sum * (1.0 / rounds);
        qInfo().noquote() << u"mean drift of the school %1 %2 %3"_s.arg(drift.x).arg(drift.y).arg(drift.z);
        QVERIFY2(!(drift.x < -2.0 && drift.z < -2.0), qPrintable(u"%1 %2"_s.arg(drift.x).arg(drift.z)));
        QVERIFY2(std::abs(drift.x) < 4.0 && std::abs(drift.z) < 4.0, qPrintable(u"%1 %2"_s.arg(drift.x).arg(drift.z)));
    }

    void releasingNeedsRoomOrTheBucketStaysFull()
    {
        QcServerConfig config;
        config.limits.breedingCrowd = 1;
        join(config);
        chat(u"/gamemode survival"_s);
        spawnFish("cod", 1.0, 0.0, 1.0, true);
        chat(u"/give cod_bucket 1"_s);
        hold("qtcraft:cod_bucket");
        const QcBlockPos ground = m_feet.offset(-2, -1, 0);
        send(QcMsg::UseItem{ground, QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(ground.offset(0, 1, 0)), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(countOf(item("qtcraft:cod_bucket")), 1);
        QCOMPARE(std::ranges::count(creatures().mobs(), type("cod"), &QcCreature::type), 1);
    }

    void fishSpawnWithinTheirOwnCap()
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcFlatGenerator>(m_content, u"bedrock,3*stone,6*water"_s));
        for (int x = -4; x <= 4; ++x) {
            for (int z = -4; z <= 4; ++z) {
                world->loadChunk({x, z});
            }
        }
        QcMobManager mobs(m_content, 7);
        mobs.spawner().setWaterCap(3);
        quint32 next = 100;
        const QList<QcMobManager::PlayerView> players{{.entityId = 1, .position = {0.5, 12.0, 0.5}}};
        for (int i = 0; i < 20 * 60; ++i) {
            mobs.tick(*world, players, 6000.0, 0.05, [&next] { return next++; });
        }
        // Only cod live in plain water; at most the cap of them, none persistent, none as animals.
        QVERIFY(!mobs.mobs().isEmpty());
        QVERIFY2(mobs.mobs().size() <= 3, qPrintable(QString::number(mobs.mobs().size())));
        for (const QcCreature &fish : mobs.mobs()) {
            QCOMPARE(fish.type, type("cod"));
            QVERIFY(!fish.persistent);
            QVERIFY(QcFishSwimming::inWater(*world, fish.motion.position + QcVec3{0.0, 0.15, 0.0}));
        }

        // Without a cap none come.
        QcMobManager none(m_content, 7);
        none.spawner().setWaterCap(0);
        for (int i = 0; i < 20 * 30; ++i) {
            none.tick(*world, players, 6000.0, 0.05, [&next] { return next++; });
        }
        QVERIFY(none.mobs().isEmpty());
    }
};

QTEST_MAIN(TestFish)
#include "test_fish.moc"
