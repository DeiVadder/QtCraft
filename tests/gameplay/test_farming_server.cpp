// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcPhysics.h"
#include "fixtures/QcServerTestFixture.h"
#include "fixtures/QcTestUtil.h"
#include "server/plants/QcServerPlants.h"

#include <cmath>

using namespace Qt::StringLiterals;

// Farming through the server (decisions E-289 to E-295): tilling, planting and eating, harvests by
// stage, seeds that plant themselves, the randomTicks rule and breeding, all over the protocol.
class TestFarmingServer : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcBlockId soil(bool wet) const { return m_content->withProperty(id("qtcraft:farmland"), u"moist"_s, wet ? u"true"_s : u"false"_s); }
    QcBlockId stage(const char *crop, int age) const
    {
        return m_content->withProperty(id(crop), u"age"_s, QString::number(age));
    }

    void hold(const char *name)
    {
        const int slot = slotOf(item(name));
        QVERIFY2(slot >= 0 && slot < 9, name);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    void use(QcBlockPos pos, QcBlockFace face = QcBlockFace::Up) { send(QcMsg::UseItem{pos, face}); }

    void mine(QcBlockPos pos)
    {
        send(QcMsg::StartMining{pos});
        send(QcMsg::FinishMining{pos});
    }

    // A resting stack of `count` items dropped on top of the block at `ground`.
    void dropOn(QcBlockPos ground, const char *name, int count)
    {
        QcItemEntityManager::Drop drop;
        drop.stack = {item(name), count, 0};
        drop.position = {ground.x + 0.5, ground.y + 1.2, ground.z + 0.5};
        QVERIFY(m_server->items().spawn(drop, 800000 + quint32(m_server->items().items().size())) != 0);
    }

    // Joins without the creatures the chunks brought: which kinds come depends on all kinds of the
    // content (a new peaceful kind changes the groups), the pigs counted are the test's own.
    void joinWithoutCreatures()
    {
        join();
        m_server->mobs().removeAll();
    }

    int pigCount() const
    {
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        return static_cast<int>(std::ranges::count(m_server->mobs().mobs(), pig, &QcCreature::type));
    }

private slots:
    // Each test starts from the base content (one test changes a crop).
    void init() { m_content = QcTestUtil::loadContent(); }

    void hoesTillGrassAndDirt()
    {
        join();
        chat(u"/give wooden_hoe"_s);
        hold("qtcraft:wooden_hoe");
        const QcBlockPos grass = m_feet.offset(2, -1, 0);
        const QcBlockPos dirt = m_feet.offset(-2, -1, 0);
        m_server->world().setBlock(dirt, id("qtcraft:dirt"));

        use(grass);
        QCOMPARE(*m_server->world().blockAt(grass), soil(false));
        use(dirt, QcBlockFace::North);
        QCOMPARE(*m_server->world().blockAt(dirt), soil(false));
        // Each tilled block wears the hoe once.
        QCOMPARE(m_client->stacks[slotOf(item("qtcraft:wooden_hoe"))].damage, 2);

        // Not from below, not under a block, not on stone.
        const QcBlockPos below = m_feet.offset(-2, -1, 2);
        use(below, QcBlockFace::Down);
        QCOMPARE(*m_server->world().blockAt(below), id("qtcraft:grass_block"));
        const QcBlockPos covered = m_feet.offset(0, -1, 2);
        m_server->world().setBlock(covered.offset(0, 1, 0), id("qtcraft:stone"));
        use(covered);
        QCOMPARE(*m_server->world().blockAt(covered), id("qtcraft:grass_block"));
        const QcBlockPos stone = m_feet.offset(0, -1, -2);
        m_server->world().setBlock(stone, id("qtcraft:stone"));
        use(stone);
        QCOMPARE(*m_server->world().blockAt(stone), id("qtcraft:stone"));
        QCOMPARE(m_client->stacks[slotOf(item("qtcraft:wooden_hoe"))].damage, 2);

        // Not out of reach, not through a wall.
        const QcBlockPos far = m_feet.offset(7, -1, 0);
        m_server->world().setBlock(far, id("qtcraft:grass_block"));
        m_server->world().setBlock(far.offset(0, 1, 0), qcAirBlockId);
        use(far);
        QCOMPARE(*m_server->world().blockAt(far), id("qtcraft:grass_block"));
        const QcBlockPos hidden = m_feet.offset(-3, -1, -2);
        for (int dy = 0; dy <= 3; ++dy) {
            for (int dz = -3; dz <= 3; ++dz) {
                m_server->world().setBlock(m_feet.offset(-2, dy, dz), id("qtcraft:stone"));
            }
        }
        use(hidden);
        QCOMPARE(*m_server->world().blockAt(hidden), id("qtcraft:grass_block"));
        QCOMPARE(m_client->stacks[slotOf(item("qtcraft:wooden_hoe"))].damage, 2);
        for (int dy = 0; dy <= 3; ++dy) {
            for (int dz = -3; dz <= 3; ++dz) {
                m_server->world().setBlock(m_feet.offset(-2, dy, dz), qcAirBlockId);
            }
        }

        // Next to water the new farmland is wet at once.
        const QcBlockPos nearWater = m_feet.offset(2, -1, 2);
        m_server->world().setBlock(m_feet.offset(3, -1, 3), id("qtcraft:water"));
        use(nearWater);
        QCOMPARE(*m_server->world().blockAt(nearWater), soil(true));
    }

    void creativeHoesDoNotWear()
    {
        QcServerConfig config;
        config.defaultGameMode = QcGameMode::Creative;
        join(config);
        chat(u"/give stone_hoe"_s);
        hold("qtcraft:stone_hoe");
        use(m_feet.offset(2, -1, 0));
        QCOMPARE(*m_server->world().blockAt(m_feet.offset(2, -1, 0)), soil(false));
        QCOMPARE(m_client->stacks[slotOf(item("qtcraft:stone_hoe"))].damage, 0);
    }

    void seedsArePlantedOnFarmlandOnly()
    {
        join();
        chat(u"/give wheat_seeds 5"_s);
        hold("qtcraft:wheat_seeds");
        const QcBlockPos field = m_feet.offset(2, -1, 0);
        m_server->world().setBlock(field, soil(false));

        use(m_feet.offset(-2, -1, 0));
        QCOMPARE(*m_server->world().blockAt(m_feet.offset(-2, 0, 0)), qcAirBlockId);
        QCOMPARE(countOf(item("qtcraft:wheat_seeds")), 5);

        use(field);
        QCOMPARE(*m_server->world().blockAt(field.offset(0, 1, 0)), stage("qtcraft:wheat_crop", 0));
        QCOMPARE(countOf(item("qtcraft:wheat_seeds")), 4);
    }

    void plantingFoodIsEatenWhereItCannotGrow()
    {
        join();
        chat(u"/give carrot 3"_s);
        chat(u"/feed 10"_s);
        ticks(2);
        QCOMPARE(m_client->food, 10);
        hold("qtcraft:carrot");

        // Aimed at grass the carrot is eaten (3 points), aimed at farmland it is planted.
        use(m_feet.offset(2, -1, 0));
        QCOMPARE(m_client->food, 13);
        QCOMPARE(countOf(item("qtcraft:carrot")), 2);
        const QcBlockPos field = m_feet.offset(-2, -1, 0);
        m_server->world().setBlock(field, soil(true));
        use(field);
        QCOMPARE(*m_server->world().blockAt(field.offset(0, 1, 0)), stage("qtcraft:carrot_crop", 0));
        QCOMPARE(m_client->food, 13);
        QCOMPARE(countOf(item("qtcraft:carrot")), 1);
    }

    void cropsDropTheirHarvestOnlyWhenRipe()
    {
        join();
        const QcBlockPos ripe = m_feet.offset(2, 0, 0);
        const QcBlockPos young = m_feet.offset(-2, 0, 0);
        for (const QcBlockPos pos : {ripe, young}) {
            m_server->world().setBlock(pos.offset(0, -1, 0), soil(true));
        }
        m_server->world().setBlock(ripe, stage("qtcraft:wheat_crop", 7));
        m_server->world().setBlock(young, stage("qtcraft:wheat_crop", 6));

        mine(young);
        QCOMPARE(*m_server->world().blockAt(young), qcAirBlockId);
        QCOMPARE(countOf(item("qtcraft:wheat_seeds")) + droppedCount(item("qtcraft:wheat_seeds")), 1);
        QCOMPARE(countOf(item("qtcraft:wheat")) + droppedCount(item("qtcraft:wheat")), 0);

        mine(ripe);
        const int seeds = countOf(item("qtcraft:wheat_seeds")) + droppedCount(item("qtcraft:wheat_seeds")) - 1;
        QCOMPARE(countOf(item("qtcraft:wheat")) + droppedCount(item("qtcraft:wheat")), 1);
        QVERIFY2(seeds >= 1 && seeds <= 3, qPrintable(QString::number(seeds)));
    }

    void droppedSeedsPlantThemselvesOnFreeFarmland()
    {
        join();
        // Three metres from the player, out of reach for picking up.
        const QcBlockPos field = m_feet.offset(3, -1, 3);
        const QcBlockPos grass = m_feet.offset(-3, -1, 3);
        const QcBlockPos dirt = m_feet.offset(-3, -1, -3);
        const QcBlockPos covered = m_feet.offset(3, -1, -3);
        m_server->world().setBlock(field, soil(false));
        m_server->world().setBlock(dirt, id("qtcraft:dirt"));
        m_server->world().setBlock(covered, soil(false));
        m_server->world().setBlock(covered.offset(0, 1, 0), stage("qtcraft:potato_crop", 1));
        dropOn(field, "qtcraft:wheat_seeds", 3);
        dropOn(grass, "qtcraft:carrot", 2);
        dropOn(dirt, "qtcraft:pumpkin_seeds", 2);
        dropOn(covered, "qtcraft:potato", 1);

        // The items land, rest and plant within a few checks (one a second, chance 0.9).
        idle(200);
        QCOMPARE(*m_server->world().blockAt(field.offset(0, 1, 0)), stage("qtcraft:wheat_crop", 0));
        // One seed planted, the rest of the stack stays (the new crop holds the place).
        QCOMPARE(droppedCount(item("qtcraft:wheat_seeds")), 2);
        QCOMPARE(*m_server->world().blockAt(grass.offset(0, 1, 0)), qcAirBlockId);
        QCOMPARE(*m_server->world().blockAt(dirt.offset(0, 1, 0)), qcAirBlockId);
        QCOMPARE(droppedCount(item("qtcraft:carrot")), 2);
        QCOMPARE(droppedCount(item("qtcraft:pumpkin_seeds")), 2);
        QCOMPARE(droppedCount(item("qtcraft:potato")), 1);
        QCOMPARE(*m_server->world().blockAt(covered.offset(0, 1, 0)), stage("qtcraft:potato_crop", 1));
    }

    void seedsPlantThemselvesOnlyOnSoil()
    {
        // A wheat crop that may also stand on dirt: placed by hand there, but dropped seeds still
        // plant themselves only on farmland (decision E-294).
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcBlockDefinition wheat = content->block(id("qtcraft:wheat_crop"));
        wheat.groundBlocks.append(u"qtcraft:dirt"_s);
        QVERIFY(content->registerBlock(wheat).has_value());
        m_content = content;
        join();

        const QcBlockPos dirt = m_feet.offset(3, -1, 3);
        m_server->world().setBlock(dirt, id("qtcraft:dirt"));
        dropOn(dirt, "qtcraft:wheat_seeds", 2);
        idle(200);
        QCOMPARE(*m_server->world().blockAt(dirt.offset(0, 1, 0)), qcAirBlockId);
        QCOMPARE(droppedCount(item("qtcraft:wheat_seeds")), 2);

        chat(u"/give wheat_seeds 1"_s);
        hold("qtcraft:wheat_seeds");
        use(m_feet.offset(-2, -1, 0));
        m_server->world().setBlock(m_feet.offset(-2, -1, 0), id("qtcraft:dirt"));
        use(m_feet.offset(-2, -1, 0));
        QCOMPARE(*m_server->world().blockAt(m_feet.offset(-2, 0, 0)), stage("qtcraft:wheat_crop", 0));
    }

    void twoStacksOnOneCellPlantOnce()
    {
        join();
        // Seeds and carrots (no merging) on one farmland cell surrounded by grass.
        const QcBlockPos field = m_feet.offset(3, -1, 3);
        m_server->world().setBlock(field, soil(false));
        dropOn(field, "qtcraft:wheat_seeds", 2);
        dropOn(field, "qtcraft:carrot", 2);
        idle(200);

        const QcBlockId planted = *m_server->world().blockAt(field.offset(0, 1, 0));
        QVERIFY(planted == stage("qtcraft:wheat_crop", 0) || planted == stage("qtcraft:carrot_crop", 0));
        QCOMPARE(droppedCount(item("qtcraft:wheat_seeds")) + droppedCount(item("qtcraft:carrot")), 3);
    }

    void aWaterFlushHarvestsAndTheSeedsReplant()
    {
        join();
        // A field of ripe wheat south of the player with a row of water sources let in behind it
        // (north); walls two blocks high keep the water and the drops in.
        const int z = m_feet.z + 3;
        const auto wall = [&](int x, int atZ) {
            for (int dy = -1; dy <= 1; ++dy) {
                m_server->world().setBlock({x, m_feet.y + dy, atZ}, id("qtcraft:stone"));
            }
        };
        for (int x = m_feet.x - 3; x <= m_feet.x + 3; ++x) {
            m_server->world().setBlock({x, m_feet.y - 1, z}, soil(true));
            m_server->world().setBlock({x, m_feet.y, z}, stage("qtcraft:wheat_crop", 7));
            m_server->world().setBlock({x, m_feet.y - 1, z - 1}, id("qtcraft:stone"));
            wall(x, z - 2);
            wall(x, z + 1);
        }
        for (const int x : {m_feet.x - 4, m_feet.x + 4}) {
            for (int atZ = z - 2; atZ <= z + 1; ++atZ) {
                wall(x, atZ);
            }
        }

        const QString row = u"%1 %2 %3 %4 %2 %3"_s.arg(m_feet.x - 3).arg(m_feet.y).arg(z - 1).arg(m_feet.x + 3);
        chat(u"/fill %1 water"_s.arg(row));
        idle(100);
        // The flow washed the crops away and pressed the drops against the south wall, each in its
        // column; take the water back and let it run off.
        int left = 0;
        for (int x = m_feet.x - 3; x <= m_feet.x + 3; ++x) {
            left += m_content->block(*m_server->world().blockAt({x, m_feet.y, z})).crop.isCrop() ? 1 : 0;
        }
        QCOMPARE(left, 0);
        chat(u"/fill %1 air"_s.arg(row));
        idle(600);

        // Seeds within a metre merge into one stack; such a stack plants the free soil around it
        // too, so the whole field grows again and the harvest lies ready for collecting.
        int replanted = 0;
        for (int x = m_feet.x - 3; x <= m_feet.x + 3; ++x) {
            replanted += *m_server->world().blockAt({x, m_feet.y, z}) == stage("qtcraft:wheat_crop", 0) ? 1 : 0;
        }
        QCOMPARE(replanted, 7);
        QCOMPARE(droppedCount(item("qtcraft:wheat")), 7);
    }

    void flowingWaterCarriesItemsAndStillWaterDoesNot()
    {
        join();
        // A channel north of the player from x - 3 to x + 3, a source at its west end.
        const int z = m_feet.z - 3;
        for (int x = m_feet.x - 4; x <= m_feet.x + 4; ++x) {
            for (int atZ = z - 1; atZ <= z + 1; ++atZ) {
                const bool inside = atZ == z && x > m_feet.x - 4 && x < m_feet.x + 4;
                m_server->world().setBlock({x, m_feet.y - 1, atZ}, id("qtcraft:stone"));
                m_server->world().setBlock({x, m_feet.y, atZ}, inside ? qcAirBlockId : id("qtcraft:stone"));
                m_server->world().setBlock({x, m_feet.y + 1, atZ}, inside ? qcAirBlockId : id("qtcraft:stone"));
            }
        }
        chat(u"/setblock %1 %2 %3 water"_s.arg(m_feet.x - 3).arg(m_feet.y).arg(z));
        idle(60);

        dropOn({m_feet.x - 2, m_feet.y - 1, z}, "qtcraft:stick", 1);
        idle(100);
        const QcItemEntity carried = m_server->items().items().constFirst();
        QVERIFY2(carried.motion.position.x > m_feet.x + 2.5, qPrintable(QString::number(carried.motion.position.x - m_feet.x)));
        QCOMPARE(QcPhysics::blockContaining(carried.motion.position).z, z);

        // A pool of sources: the item floats where it is.
        chat(u"/fill %1 %2 %3 %4 %2 %3 water"_s.arg(m_feet.x - 3).arg(m_feet.y).arg(z).arg(m_feet.x + 3));
        idle(20);
        const QcVec3 before = m_server->items().items().constFirst().motion.position;
        idle(100);
        const QcVec3 after = m_server->items().items().constFirst().motion.position;
        QVERIFY(std::abs(after.x - before.x) < 1e-6);
        QVERIFY(std::abs(after.z - before.z) < 1e-6);
    }

    void theRandomTicksRuleStopsGrowth()
    {
        join();
        // 49 young plants on wet farmland next to water, east of the player.
        const QcBlockPos corner = m_feet.offset(8, -1, -3);
        for (int dx = 0; dx < 7; ++dx) {
            for (int dz = 0; dz < 7; ++dz) {
                m_server->world().setBlock(corner.offset(dx, 0, dz), soil(true));
                m_server->world().setBlock(corner.offset(dx, 1, dz), stage("qtcraft:wheat_crop", 0));
                for (int dy = 2; dy < 40; ++dy) {
                    m_server->world().setBlock(corner.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        m_server->world().setBlock(corner.offset(3, 0, 7), id("qtcraft:water"));
        const auto stages = [&] {
            int total = 0;
            for (int dx = 0; dx < 7; ++dx) {
                for (int dz = 0; dz < 7; ++dz) {
                    total += m_content->property(*m_server->world().blockAt(corner.offset(dx, 1, dz)), u"age"_s).toInt();
                }
            }
            return total;
        };

        chat(u"/gamerule randomTicks false"_s);
        QVERIFY(m_client->chatLines.contains(u"randomTicks is now false"_s));
        ticks(2400);
        QCOMPARE(stages(), 0);

        // 120 s: about 0.7 stages per plant.
        chat(u"/gamerule randomTicks true"_s);
        ticks(2400);
        QVERIFY2(stages() >= 15, qPrintable(QString::number(stages())));
    }

    void fedAnimalsGetYoungOnes()
    {
        joinWithoutCreatures();
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        const quint32 first = m_server->mobs().spawn(pig, QcVec3{m_feet.x + 2.5, static_cast<double>(m_feet.y), m_feet.z + 0.5}, 900001, false, true);
        const quint32 second = m_server->mobs().spawn(pig, QcVec3{m_feet.x - 1.5, static_cast<double>(m_feet.y), m_feet.z + 2.5}, 900002, false, true);
        chat(u"/give carrot 5"_s);
        chat(u"/give wheat 1"_s);
        hold("qtcraft:wheat");

        // Pigs do not eat wheat; that is eaten by the player instead (full: nothing happens).
        send(QcMsg::UseEntity{first});
        QCOMPARE(m_server->mobs().find(first)->love, 0.0);
        QCOMPARE(countOf(item("qtcraft:wheat")), 1);

        hold("qtcraft:carrot");
        send(QcMsg::UseEntity{first});
        QVERIFY(m_server->mobs().find(first)->love > 0.0);
        QCOMPARE(countOf(item("qtcraft:carrot")), 4);
        QCOMPARE(pigCount(), 2);
        send(QcMsg::UseEntity{second});
        // Both court for a second (20 ticks) before the young one comes.
        ticks(18);
        QCOMPARE(pigCount(), 2);
        ticks(2);
        QCOMPARE(pigCount(), 3);
        QCOMPARE(countOf(item("qtcraft:carrot")), 3);

        const auto young = std::ranges::find_if(m_server->mobs().mobs(), &QcCreature::isYoung);
        QVERIFY(young != m_server->mobs().mobs().end());
        const quint32 youngId = young->id;
        QVERIFY(young->persistent);
        QVERIFY(young->growUp > 1190.0);
        QCOMPARE(m_server->mobs().find(first)->love, 0.0);
        QVERIFY(m_server->mobs().find(first)->breedCooldown > 290.0);
        idle(2);
        QVERIFY(m_client->flags.value(youngId) & QcMsg::Young);
        QVERIFY(!(m_client->flags.value(first) & QcMsg::Young));

        // Resting parents do not eat; the young one eats and grows up sooner.
        send(QcMsg::UseEntity{first});
        QCOMPARE(countOf(item("qtcraft:carrot")), 3);
        const double before = m_server->mobs().find(youngId)->growUp;
        send(QcMsg::UseEntity{youngId});
        QCOMPARE(countOf(item("qtcraft:carrot")), 2);
        QVERIFY(m_server->mobs().find(youngId)->growUp < before - 100.0);

        // It grows up when its time is over; until then it leaves nothing behind.
        const std::optional<QList<QcItemDrop>> drops = m_server->mobs().damage({.creature = second, .damage = 100.0, .from = m_client->motion.position});
        QVERIFY(drops && !drops->isEmpty());
        m_server->mobs().find(youngId)->growUp = 0.1;
        ticks(3);
        QVERIFY(!m_server->mobs().find(youngId)->isYoung());
    }

    void animalsBreedOnlyWhenTheySeeEachOther()
    {
        joinWithoutCreatures();
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        // A wall three blocks high between two pigs in love, long enough not to walk around in time.
        for (int x = m_feet.x - 6; x <= m_feet.x + 6; ++x) {
            for (int dy = 0; dy < 3; ++dy) {
                m_server->world().setBlock({x, m_feet.y + dy, m_feet.z + 2}, id("qtcraft:stone"));
            }
        }
        const quint32 north = m_server->mobs().spawn(pig, QcVec3{m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z + 1.5}, 900021);
        const quint32 south = m_server->mobs().spawn(pig, QcVec3{m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z + 3.5}, 900022);
        for (const quint32 id : {north, south}) {
            m_server->mobs().find(id)->love = 30.0;
        }
        ticks(60);
        QCOMPARE(pigCount(), 2);

        // Without the wall they pair; the young one appears at the feet of a parent.
        for (int x = m_feet.x - 6; x <= m_feet.x + 6; ++x) {
            for (int dy = 0; dy < 3; ++dy) {
                m_server->world().setBlock({x, m_feet.y + dy, m_feet.z + 2}, qcAirBlockId);
            }
        }
        QcVec3 parent;
        for (int i = 0; i < 40 && pigCount() == 2; ++i) {
            parent = m_server->mobs().find(north)->motion.position;
            ticks(1);
        }
        QCOMPARE(pigCount(), 3);
        const auto young = std::ranges::find_if(m_server->mobs().mobs(), &QcCreature::isYoung);
        QVERIFY(young->bred);
        QVERIFY(young->persistent);
        QVERIFY2((young->motion.position - parent).length() < 0.5, qPrintable(QString::number((young->motion.position - parent).length())));
    }

    void youngOnesAreBornOnTheGroundOfAParent()
    {
        joinWithoutCreatures();
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        // One parent on the ground, the other on a pillar two blocks high, both in sight.
        const QcBlockPos pillar = m_feet.offset(2, 0, 2);
        m_server->world().setBlock(pillar, id("qtcraft:stone"));
        m_server->world().setBlock(pillar.offset(0, 1, 0), id("qtcraft:stone"));
        const quint32 below = m_server->mobs().spawn(pig, QcVec3{m_feet.x - 1.5, static_cast<double>(m_feet.y), m_feet.z + 0.5}, 900031);
        const quint32 above = m_server->mobs().spawn(pig, QcVec3{pillar.x + 0.5, pillar.y + 2.0, pillar.z + 0.5}, 900032);
        for (const quint32 id : {below, above}) {
            m_server->mobs().find(id)->love = 30.0;
        }
        for (int i = 0; i < 40 && pigCount() == 2; ++i) {
            ticks(1);
        }
        QCOMPARE(pigCount(), 3);
        const auto young = std::ranges::find_if(m_server->mobs().mobs(), &QcCreature::isYoung);
        // Not in the air between them (the midpoint would float a block up).
        QVERIFY(std::abs(young->motion.position.y - m_feet.y) < 0.01 || std::abs(young->motion.position.y - (pillar.y + 2.0)) < 0.01);
    }

    void restingAnimalsLeaveTheirFoodInTheHand()
    {
        join();
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        const quint32 resting = m_server->mobs().spawn(pig, QcVec3{m_feet.x + 2.5, static_cast<double>(m_feet.y), m_feet.z + 0.5}, 900041);
        m_server->mobs().find(resting)->breedCooldown = 100.0;
        chat(u"/give carrot 2"_s);
        chat(u"/feed 10"_s);
        hold("qtcraft:carrot");
        send(QcMsg::UseEntity{resting});
        QCOMPARE(countOf(item("qtcraft:carrot")), 2);
        QCOMPARE(m_client->food, 10);
    }

    void foodAndArmourAimedAtCreaturesAreUsedByThePlayer()
    {
        join();
        const quint32 zombie = m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:zombie"_s),
                                                      QcVec3{m_feet.x + 2.5, static_cast<double>(m_feet.y), m_feet.z + 0.5}, 900051);
        chat(u"/give bread 1"_s);
        chat(u"/give iron_helmet"_s);
        chat(u"/feed 10"_s);
        hold("qtcraft:bread");
        send(QcMsg::UseEntity{zombie});
        QCOMPARE(m_client->food, 15);
        QCOMPARE(countOf(item("qtcraft:bread")), 0);

        hold("qtcraft:iron_helmet");
        send(QcMsg::UseEntity{zombie});
        QCOMPARE(countOf(item("qtcraft:iron_helmet")), 0);
        QCOMPARE(m_client->equipment.stacks[0].item, item("qtcraft:iron_helmet"));
    }

    void youngAnimalsAreSmallerTargets()
    {
        join();
        // A pig in a pit one block deep, three metres away: the top of an adult shows over the edge,
        // a young one at half size stays hidden.
        const QcBlockPos pit = m_feet.offset(3, -1, 0);
        m_server->world().setBlock(pit, qcAirBlockId);
        m_server->world().setBlock(pit.offset(0, -1, 0), id("qtcraft:stone"));
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        const quint32 id = m_server->mobs().spawn(pig, QcVec3{pit.x + 0.5, static_cast<double>(pit.y), pit.z + 0.5}, 900061);
        chat(u"/give carrot 4"_s);
        hold("qtcraft:carrot");

        m_server->mobs().find(id)->growUp = 600.0;
        send(QcMsg::UseEntity{id});
        QCOMPARE(m_server->mobs().find(id)->growUp, 600.0 - 0.05);
        QCOMPARE(countOf(item("qtcraft:carrot")), 4);

        m_server->mobs().find(id)->growUp = 0.0;
        send(QcMsg::UseEntity{id});
        QVERIFY(m_server->mobs().find(id)->love > 0.0);
        QCOMPARE(countOf(item("qtcraft:carrot")), 3);
    }

    void youngAnimalsDropNothing()
    {
        join();
        const int sheep = *m_content->mobIndex(u"qtcraft:sheep"_s);
        const quint32 lamb = m_server->mobs().spawn(sheep, QcVec3{m_feet.x + 2.5, static_cast<double>(m_feet.y), m_feet.z + 0.5}, 900010);
        m_server->mobs().find(lamb)->growUp = 600.0;
        const std::optional<QList<QcItemDrop>> drops = m_server->mobs().damage({.creature = lamb, .damage = 100.0, .from = m_client->motion.position});
        QVERIFY(drops.has_value());
        QVERIFY(drops->isEmpty());
    }
};

QTEST_MAIN(TestFarmingServer)
#include "test_farming_server.moc"
