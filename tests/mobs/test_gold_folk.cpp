// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/mobs/QcMobReader.h"
#include "common/content/mobs/QcMobTraitsContent.h"
#include "common/content/mobs/QcSpawnWeights.h"
#include "mobs/QcGoldFolkFixture.h"
#include "server/animals/QcServerBarter.h"
#include "server/mobbrains/QcMobSteering.h"

#include <QJsonDocument>

#include <algorithm>

// The gold-loving tuskling and the bristleboar of the nether (decisions E-1081 to E-1083): foes of
// who wears no gold, anger at gold mining and opened chests, young ones, breeding with crimson
// fungus, fear of blocks, spawn weights per biome and what the content says about them. Bartering
// has its own test (test_barter). Ticks are stepped by hand.
class TestGoldFolk : public QcGoldFolkFixture
{
    Q_OBJECT

private:
    int youngBoars() const
    {
        const QcCreatureKindId boars = kind(u"bristleboar"_s);
        return static_cast<int>(std::ranges::count_if(m_server->mobs().mobs(), [&](const QcCreature &creature) { return creature.type == boars && creature.isYoung(); }));
    }

    // Walls and a roof of stone around the player: nobody sees him any more.
    void shutThePlayerIn()
    {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dy = 0; dy <= 2; ++dy) {
                    if (dx != 0 || dz != 0 || dy == 2) {
                        m_server->world().setBlock(m_feet.offset(dx, dy, dz), id("qtcraft:stone"));
                    }
                }
            }
        }
    }

private slots:
    void theDataFollowTheRoleModel()
    {
        const QcMobDefinition &tuskling = m_content->mobs()[kind(u"tuskling"_s)];
        QCOMPARE(tuskling.maxHealth, 16.0);
        QCOMPARE(tuskling.angerSeconds, 30.0);
        QCOMPARE(tuskling.packRange, 16.0);
        QCOMPARE(tuskling.youngChance, 0.2);
        QVERIFY(tuskling.staysYoung);
        QVERIFY(tuskling.barter);
        QCOMPARE(tuskling.barter->admireSeconds, 6.0);
        QCOMPARE(tuskling.barter->currency, u"qtcraft:gold_ingot"_s);
        const QcMobDefinition &boar = m_content->mobs()[kind(u"bristleboar"_s)];
        QCOMPARE(boar.maxHealth, 40.0);
        QVERIFY(boar.breeding.food.contains(u"qtcraft:crimson_fungus"_s));
        QVERIFY(boar.repellents && boar.repellents->blocks.contains(u"qtcraft:warped_fungus"_s));
        QVERIFY(tuskling.barter->loves.contains(u"qtcraft:bell"_s));
    }

    void spawnWeightsPerBiome_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("biome");
        QTest::addColumn<double>("weight");
        QTest::newRow("tuskling in the wastes") << u"tuskling"_s << u"qtcraft:nether_wastes"_s << 15.0;
        QTest::newRow("tuskling in the crimson forest") << u"tuskling"_s << u"qtcraft:crimson_forest"_s << 5.0;
        QTest::newRow("no tuskling in the warped forest") << u"tuskling"_s << u"qtcraft:warped_forest"_s << 0.0;
        QTest::newRow("no tuskling in the soul sand valley") << u"tuskling"_s << u"qtcraft:soul_sand_valley"_s << 0.0;
        QTest::newRow("no tuskling in the deltas") << u"tuskling"_s << u"qtcraft:basalt_deltas"_s << 0.0;
        QTest::newRow("bristleboar in the crimson forest") << u"bristleboar"_s << u"qtcraft:crimson_forest"_s << 9.0;
        QTest::newRow("no bristleboar in the wastes") << u"bristleboar"_s << u"qtcraft:nether_wastes"_s << 0.0;
        QTest::newRow("no bristleboar in the warped forest") << u"bristleboar"_s << u"qtcraft:warped_forest"_s << 0.0;
        QTest::newRow("no bristleboar in the overworld") << u"bristleboar"_s << u"qtcraft:plains"_s << 0.0;
    }

    void spawnWeightsPerBiome()
    {
        QFETCH(QString, name);
        QFETCH(QString, biome);
        QFETCH(double, weight);
        const QcSpawnWeights weights(*m_content);
        QCOMPARE(weights.weight(kind(name), biome), weight);
        QCOMPARE(m_content->mobs()[kind(name)].maxGroup, 4);
    }

    void aPlayerWithoutGoldIsAFoe_data()
    {
        QTest::addColumn<bool>("gold");
        QTest::newRow("no gold") << false;
        QTest::newRow("a golden helmet") << true;
    }

    void aPlayerWithoutGoldIsAFoe()
    {
        QFETCH(bool, gold);
        joinPasture();
        if (gold) {
            wearGold();
        }
        const quint32 tuskling = spawn(u"tuskling"_s, 4, 0);
        ticks(40);
        QCOMPARE(mob(tuskling).chasing, gold ? 0u : player().id);
        if (!gold) {
            QVERIFY(player().health < QcServerClient::maxHealth);
        }
    }

    void aYoungOneNeverFightsNorGrowsUp()
    {
        joinPasture();
        const quint32 young = spawn(u"tuskling"_s, 3, 0);
        m_server->mobs().find(young)->growUp = 1200.0;
        ticks(200);
        QCOMPARE(mob(young).chasing, 0u);
        QCOMPARE(mob(young).growUp, 1200.0);
        QCOMPARE(player().health, QcServerClient::maxHealth);
    }

    void goldMinedAngersEveryOneNearby()
    {
        joinPasture();
        wearGold();
        wallEast();
        // One behind the wall, which still hears it, and one far off.
        const quint32 hidden = spawn(u"tuskling"_s, 4, 0);
        const quint32 far = spawn(u"tuskling"_s, -20, 0);
        const QcBlockPos gold = m_feet.offset(-1, 0, 1);
        m_server->world().setBlock(gold, id("qtcraft:gold_block"));
        hold("qtcraft:iron_pickaxe");
        ticks(20);
        QCOMPARE(mob(hidden).provokedBy, 0u);
        mine(gold);
        ticks(12);
        QCOMPARE(m_server->world().blockAt(gold), qcAirBlockId);
        QCOMPARE(mob(hidden).provokedBy, player().id);
        QCOMPARE(mob(far).provokedBy, 0u);
        QCOMPARE(mob(hidden).angerTimer > 25.0, true);
    }

    void aChestOpenedInSightAngers_data()
    {
        QTest::addColumn<QString>("block");
        QTest::addColumn<bool>("walled");
        QTest::addColumn<bool>("angers");
        QTest::newRow("a chest in sight") << u"qtcraft:chest"_s << false << true;
        QTest::newRow("a barrel in sight") << u"qtcraft:barrel"_s << false << true;
        QTest::newRow("a chest behind a wall") << u"qtcraft:chest"_s << true << false;
        QTest::newRow("a furnace in sight") << u"qtcraft:furnace"_s << false << false;
    }

    // With its own mind: the opening reaches the brain, which turns the creature on the player.
    void aChestOpenedInSightAngers()
    {
        QFETCH(QString, block);
        QFETCH(bool, walled);
        QFETCH(bool, angers);
        joinPasture();
        wearGold();
        if (walled) {
            wallEast();
        }
        const quint32 tuskling = spawn(u"tuskling"_s, 4, 0);
        const QcBlockPos chest = m_feet.offset(-1, 0, 0);
        m_server->world().setBlock(chest, *m_content->blockId(block));
        ticks(2);
        QCOMPARE(mob(tuskling).provokedBy, 0u);
        send(QcMsg::UseItem{chest});
        ticks(QcMobSteering::sightCheckTicks + 1);
        QCOMPARE(mob(tuskling).provokedBy, angers ? player().id : 0u);
        QCOMPARE(mob(tuskling).chasing, angers ? player().id : 0u);
        QCOMPARE(mob(tuskling).angerTimer > 25.0, angers);
    }

    void onlyWhoMayBeAttackedIsAlarmedAt_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<bool>("angers");
        QTest::newRow("survival") << u"survival"_s << true;
        QTest::newRow("creative") << u"creative"_s << false;
        QTest::newRow("spectator") << u"spectator"_s << false;
    }

    void onlyWhoMayBeAttackedIsAlarmedAt()
    {
        QFETCH(QString, mode);
        QFETCH(bool, angers);
        joinPasture(mode);
        wearGold();
        const quint32 tuskling = spawn(u"tuskling"_s, 4, 0);
        ticks(2);
        m_server->systems().animals.barter().alarm(player(), u"qtcraft:gold_block"_s, false);
        ticks(QcMobSteering::sightCheckTicks + 1);
        QCOMPARE(mob(tuskling).provokedBy, angers ? player().id : 0u);
        QCOMPARE(mob(tuskling).chasing, angers ? player().id : 0u);
    }

    // A blow reaches the others of its kind within packRange, but no young one.
    void aBlowAngersThePack()
    {
        joinPasture();
        wearGold();
        const quint32 struck = spawn(u"tuskling"_s, 2, 0);
        const quint32 near = spawn(u"tuskling"_s, 8, 0);
        const quint32 far = spawn(u"tuskling"_s, -20, 0);
        const quint32 young = spawn(u"tuskling"_s, 8, 2);
        m_server->mobs().find(young)->growUp = 1200.0;
        const quint32 boar = spawn(u"bristleboar"_s, 6, 4);
        m_server->mobs().find(boar)->noAi = true;
        ticks(2);
        send(QcMsg::AttackEntity{struck});
        ticks(QcMobSteering::sightCheckTicks + 1);
        QCOMPARE(mob(struck).provokedBy, player().id);
        QCOMPARE(mob(near).provokedBy, player().id);
        QCOMPARE(mob(far).provokedBy, 0u);
        QCOMPARE(mob(young).provokedBy, 0u);
        QCOMPARE(mob(boar).provokedBy, 0u);
    }

    void boarsBreedWithCrimsonFungus()
    {
        joinPasture(u"creative"_s);
        const quint32 first = spawn(u"bristleboar"_s, 2, 0);
        const quint32 second = spawn(u"bristleboar"_s, 2, 2);
        hold("qtcraft:crimson_fungus");
        send(QcMsg::UseEntity{first});
        send(QcMsg::UseEntity{second});
        ticks(1);
        QVERIFY(mob(first).love > 0.0);
        QVERIFY(mob(second).love > 0.0);
        QCOMPARE(youngBoars(), 0);
        QTRY_VERIFY_WITH_TIMEOUT((ticks(10), youngBoars() == 1), 30000);
        QVERIFY(mob(first).breedCooldown > 0.0);
    }

    // In survival they hunt the player who fed them; once he is out of sight they turn to love.
    void boarsBreedInSurvival()
    {
        joinPasture();
        const quint32 first = spawn(u"bristleboar"_s, 3, 0);
        const quint32 second = spawn(u"bristleboar"_s, 3, 3);
        chat(u"/give crimson_fungus 2"_s);
        ticks(1);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:crimson_fungus"))});
        send(QcMsg::UseEntity{first});
        send(QcMsg::UseEntity{second});
        QCOMPARE(player().inventory.countOf(item("qtcraft:crimson_fungus")), 0);
        QVERIFY(mob(first).love > 0.0 && mob(second).love > 0.0);
        shutThePlayerIn();
        QTRY_VERIFY_WITH_TIMEOUT((ticks(10), youngBoars() == 1), 30000);
    }

    // Love does not stop the hunt: the partner's goal is not forced on a chaser.
    void aHuntingBoarInLoveKeepsRunning()
    {
        joinPasture();
        const quint32 boar = spawn(u"bristleboar"_s, 8, 0);
        const quint32 partner = spawn(u"bristleboar"_s, 8, 3);
        ticks(QcMobSteering::sightCheckTicks + 1);
        QCOMPARE(mob(boar).chasing, player().id);
        const double start = (mob(boar).motion.position - player().motion.position).length();
        m_server->mobs().find(boar)->love = 30.0;
        m_server->mobs().find(partner)->love = 30.0;
        ticks(30);
        QVERIFY2((mob(boar).motion.position - player().motion.position).length() < start - 2.0, "it stood still");
        QCOMPARE(mob(boar).path.purpose, QcMobPath::Purpose::Chase);
    }

    // A wild boar would go with the distance; one a player fed stays with the world.
    void aFedBoarIsKept()
    {
        joinPasture(u"creative"_s);
        const quint32 boar = spawn(u"bristleboar"_s, 2, 0);
        QVERIFY(!mob(boar).persistent);
        hold("qtcraft:crimson_fungus");
        send(QcMsg::UseEntity{boar});
        QVERIFY(mob(boar).love > 0.0);
        QVERIFY(mob(boar).persistent);
    }

    void boarsFleeFromBlocks_data()
    {
        QTest::addColumn<QString>("block");
        QTest::newRow("warped fungus") << u"qtcraft:warped_fungus"_s;
        QTest::newRow("a portal") << u"qtcraft:nether_portal"_s;
    }

    void boarsFleeFromBlocks()
    {
        QFETCH(QString, block);
        joinPasture();
        const quint32 boar = spawn(u"bristleboar"_s, 5, 0);
        const QcBlockPos fungus = m_feet.offset(3, 0, 0);
        m_server->world().setBlock(fungus, *m_content->blockId(block));
        ticks(QcMobSteering::sightCheckTicks + 1);
        QVERIFY(mob(boar).fleeTimer > 0.0);
        QCOMPARE(mob(boar).chasing, 0u);
        const QcVec3 centre{fungus.x + 0.5, static_cast<double>(fungus.y), fungus.z + 0.5};
        const double start = (mob(boar).motion.position - centre).length();
        ticks(40);
        QVERIFY((mob(boar).motion.position - centre).length() > start + 1.0);
        QCOMPARE(player().health, QcServerClient::maxHealth);
    }

    void aBoarWithoutFungusAttacks()
    {
        joinPasture();
        const quint32 boar = spawn(u"bristleboar"_s, 4, 0);
        ticks(60);
        QCOMPARE(mob(boar).chasing, player().id);
        QVERIFY(player().health < QcServerClient::maxHealth);
    }

    void aBoarDropsPorkAndLeather()
    {
        joinPasture();
        int pork = 0;
        for (int i = 0; i < 40; ++i) {
            const std::optional<QList<QcItemDrop>> drops = m_server->mobs().wound(spawn(u"bristleboar"_s, 3, 0), 1000.0);
            QVERIFY(drops);
            for (const QcItemDrop &drop : *drops) {
                if (drop.item == u"qtcraft:raw_porkchop"_s) {
                    QVERIFY(drop.count == 2 && drop.maxCount == 4);
                    ++pork;
                }
            }
        }
        QCOMPARE(pork, 40);
    }

    // A typo in these lists would quietly do nothing.
    void unknownNamesAreWarnedOf()
    {
        QcMobDefinition tuskling = m_content->mobs()[kind(u"tuskling"_s)];
        QCOMPARE(QcMobTraitsContent::resolve(*m_content, tuskling).warnings, QStringList{});
        tuskling.barter->sparedBy.append(u"qtcraft:no_helmet"_s);
        tuskling.barter->loves.append(u"qtcraft:no_gold"_s);
        tuskling.barter->guards.append(u"qtcraft:no_chest"_s);
        tuskling.barter->currency = u"qtcraft:no_ingot"_s;
        tuskling.barter->table = u"qtcraft:no_table"_s;
        tuskling.repellents = QcRepellentDefinition{.blocks = {u"qtcraft:no_fungus"_s}};
        const QString warnings = QcMobTraitsContent::resolve(*m_content, tuskling).warnings.join(u'\n');
        for (const QString &name : {u"no_helmet"_s, u"no_gold"_s, u"no_chest"_s, u"no_ingot"_s, u"no_table"_s, u"no_fungus"_s}) {
            QVERIFY2(warnings.contains(name), qPrintable(name));
        }
    }

    // Only the passive brain and the hostile one on top of it look for such blocks.
    void repelledByNeedsABrainThatLooks()
    {
        const auto mobWith = [](const char *ai) {
            const QByteArray json = "{\"name\": \"m\", \"hostile\": true, \"skeleton\": \"humanoid\", \"outfit\": \"zombie\", \"ai\": \"" + QByteArray(ai)
                                    + "\", \"repelledBy\": {\"blocks\": [\"warped_fungus\"]}}";
            return QcMobReader(QJsonDocument::fromJson(json).object(), u"qtcraft"_s).read();
        };
        QVERIFY(mobWith("hostile"));
        QVERIFY(mobWith("passive"));
        const QcResult<QcMobDefinition> neutral = mobWith("neutral");
        QVERIFY(!neutral);
        QVERIFY2(neutral.error().contains(u"repelledBy"_s), qPrintable(neutral.error()));
    }

};

QTEST_GUILESS_MAIN(TestGoldFolk)
#include "test_gold_folk.moc"
