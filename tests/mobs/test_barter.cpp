// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcContentLoot.h"
#include "mobs/QcGoldFolkFixture.h"
#include "server/animals/QcServerBarter.h"
#include "server/dimensions/QcEntityTravel.h"
#include "server/mobbrains/QcMobSteering.h"

#include <QElapsedTimer>
#include <QTemporaryDir>

#include <algorithm>

// Bartering of the tuskling (decision E-1081): the currency from the hand, loved items from the
// floor, what it refuses, its hoard as the stacks they were at every death, in a save and through a
// portal. Ticks are stepped by hand.
class TestBarter : public QcGoldFolkFixture
{
    Q_OBJECT

private:
    // A worn, enchanted, named golden hoe: everything a stack can lose on the way.
    QcItemStack treasure() const
    {
        const QcItemStack worn{item("qtcraft:golden_hoe"), 1, 7};
        return worn.withEnchantments({{u"qtcraft:unbreaking"_s, 2}}).withName(u"Grandfather's"_s);
    }

    // A tuskling that kept `treasure()` and looks at a named gold ingot.
    quint32 richTuskling(double dx, double dz)
    {
        const quint32 tuskling = spawn(u"tuskling"_s, dx, dz);
        QcCreature &living = *m_server->mobs().find(tuskling);
        living.barter->hoard = {treasure()};
        living.barter->admiring = namedIngot();
        living.barter->admireSeconds = 60.0;
        return tuskling;
    }

    QcItemStack namedIngot() const { return QcItemStack{item("qtcraft:gold_ingot"), 1, 0}.withName(u"Offering"_s); }

    // Solid stone around the cell, so that a creature there has nowhere to go and suffocates.
    void buryAt(int dx, int dz)
    {
        for (int x = -4; x <= 4; ++x) {
            for (int z = -4; z <= 4; ++z) {
                for (int y = -2; y <= 8; ++y) {
                    m_server->world().setBlock(m_feet.offset(dx + x, y, dz + z), id("qtcraft:stone"));
                }
            }
        }
    }

    // Whether exactly one stack equal to `stack` lies around.
    bool lies(const QcItemStack &stack) const
    {
        const QList<QcItemEntity> &items = m_server->entities().items().items();
        return std::ranges::count_if(items, [&](const QcItemEntity &entity) { return entity.stack == stack; }) == 1;
    }

private slots:
    void aGoldIngotIsBarteredByTheTable()
    {
        joinPasture();
        wearGold();
        const quint32 tuskling = spawn(u"tuskling"_s, 1, 0);
        hold("qtcraft:gold_ingot");
        const int ingots = player().inventory.countOf(item("qtcraft:gold_ingot"));
        send(QcMsg::UseEntity{tuskling});
        ticks(1);
        QVERIFY(mob(tuskling).barter->admires());
        QCOMPARE(player().inventory.countOf(item("qtcraft:gold_ingot")), ingots - 1);
        QVERIFY(mob(tuskling).persistent);
        // It stands while it looks.
        const QcVec3 before = mob(tuskling).motion.position;
        ticks(100);
        QVERIFY(mob(tuskling).barter->admires());
        QVERIFY((mob(tuskling).motion.position - before).length() < 0.3);
        ticks(25);
        QVERIFY(!mob(tuskling).barter->admires());
        const QHash<QString, int> loot = lyingAround();
        QVERIFY2(!loot.isEmpty(), "nothing bartered");
        const QcLootTableDefinition *table = m_content->lootTables().find(u"qtcraft:tuskling_barter"_s);
        QVERIFY(table);
        for (auto it = loot.begin(); it != loot.end(); ++it) {
            const bool listed = std::ranges::any_of(table->pools.front().entries, [&](const auto &entry) { return entry.item == it.key(); });
            QVERIFY2(listed, qPrintable(it.key()));
        }
        QVERIFY(!loot.contains(u"qtcraft:gold_ingot"_s));
    }

    void barterDrawsFollowTheWeights()
    {
        const QcLootTableDefinition *table = m_content->lootTables().find(u"qtcraft:tuskling_barter"_s);
        QVERIFY(table);
        double total = 0.0;
        for (const auto &entry : table->pools.front().entries) {
            total += entry.weight;
        }
        QCOMPARE(total, 406.0);
        constexpr int draws = 20000;
        QHash<QString, int> seen;
        for (quint64 seed = 1; seed <= draws; ++seed) {
            const QList<QcItemStack> stacks = QcContentLoot::draw(*m_content, u"qtcraft:tuskling_barter"_s, seed);
            QVERIFY(!stacks.isEmpty());
            ++seen[m_content->item(stacks.front().item).name];
        }
        for (const auto &entry : table->pools.front().entries) {
            const double share = static_cast<double>(seen.value(entry.item)) / draws;
            QVERIFY2(std::abs(share - entry.weight / total) < 0.012, qPrintable(u"%1: %2"_s.arg(entry.item).arg(share)));
        }
        QCOMPARE(QcContentLoot::draw(*m_content, u"qtcraft:tuskling_barter"_s, 77), QcContentLoot::draw(*m_content, u"qtcraft:tuskling_barter"_s, 77));
    }

    void itPicksUpGoldButNotStone()
    {
        joinPasture();
        wearGold();
        const quint32 tuskling = spawn(u"tuskling"_s, 0, 5);
        lay(QcItemStack{item("qtcraft:stone"), 1, 0}, 3, 5);
        ticks(200);
        QVERIFY(!mob(tuskling).barter->admires());
        QCOMPARE(lyingAround().value(u"qtcraft:stone"_s), 1);
        lay(QcItemStack{item("qtcraft:golden_hoe"), 1, 0}, 3, 5);
        QTRY_VERIFY_WITH_TIMEOUT((ticks(5), mob(tuskling).barter->admires()), 30000);
        QVERIFY(!lyingAround().contains(u"qtcraft:golden_hoe"_s));
        ticks(130);
        // A loved item that is no currency goes to its hoard and falls when it dies.
        QCOMPARE(mob(tuskling).barter->hoard.size(), 1);
        QVERIFY(m_server->mobs().wound(tuskling, 1000.0).has_value());
        QCOMPARE(lyingAround().value(u"qtcraft:golden_hoe"_s), 1);
    }

    // What it takes stays the stack it was: a worn tool comes back worn, not mended.
    void aStackKeepsItsDamageEnchantmentsAndName()
    {
        joinPasture();
        wearGold();
        const quint32 tuskling = spawn(u"tuskling"_s, -3, 5);
        lay(treasure(), -3, 6);
        QTRY_VERIFY_WITH_TIMEOUT((ticks(5), mob(tuskling).barter->admires()), 30000);
        QCOMPARE(mob(tuskling).barter->admiring, treasure());
        ticks(125);
        QCOMPARE(mob(tuskling).barter->hoard, QList<QcItemStack>{treasure()});
        QVERIFY(m_server->mobs().wound(tuskling, 1000.0).has_value());
        QVERIFY(lies(treasure()));
    }

    void itTakesOneOfAStackLying()
    {
        joinPasture();
        wearGold();
        const QcItemStack ingots = QcItemStack{item("qtcraft:gold_ingot"), 3, 0}.withName(u"Offering"_s);
        const quint32 tuskling = spawn(u"tuskling"_s, -3, 5);
        lay(ingots, -3, 6);
        QTRY_VERIFY_WITH_TIMEOUT((ticks(5), mob(tuskling).barter->admires()), 30000);
        QCOMPARE(mob(tuskling).barter->admiring, namedIngot());
        QVERIFY(lies(ingots.withCount(2)));
    }

    // Other loved items wait until there is room, the currency never does.
    void aFullHoardTakesOnlyTheCurrency()
    {
        joinPasture();
        wearGold();
        const quint32 tuskling = spawn(u"tuskling"_s, -3, 5);
        QcCreature &living = *m_server->mobs().find(tuskling);
        const int room = m_content->mobs()[living.type].barter->hoardSize;
        living.barter->hoard = QList<QcItemStack>(room, QcItemStack{item("qtcraft:golden_axe"), 1, 0});
        lay(QcItemStack{item("qtcraft:golden_hoe"), 1, 0}, -3, 6);
        ticks(100);
        QVERIFY(!mob(tuskling).barter->admires());
        QCOMPARE(lyingAround().value(u"qtcraft:golden_hoe"_s), 1);
        lay(QcItemStack{item("qtcraft:gold_ingot"), 1, 0}, -3, 4);
        QTRY_VERIFY_WITH_TIMEOUT((ticks(5), mob(tuskling).barter->admires()), 30000);
        QCOMPARE(mob(tuskling).barter->admiring.item, item("qtcraft:gold_ingot"));
        QCOMPARE(mob(tuskling).barter->hoard.size(), room);
    }

    void refusals_data()
    {
        QTest::addColumn<QString>("why");
        for (const char *why : {"young", "far", "walled", "angry", "busy"}) {
            QTest::newRow(why) << QString::fromLatin1(why);
        }
    }

    // A refusal costs nothing, the ingot stays in the hand and the creature takes nothing.
    void refusals()
    {
        QFETCH(QString, why);
        joinPasture();
        wearGold();
        if (why == u"walled"_s) {
            wallEast();
        }
        const quint32 tuskling = spawn(u"tuskling"_s, why == u"far"_s ? 9 : (why == u"walled"_s ? 3 : 1), 0);
        QcCreature &living = *m_server->mobs().find(tuskling);
        hold("qtcraft:gold_ingot");
        const int ingots = player().inventory.countOf(item("qtcraft:gold_ingot"));
        if (why == u"young"_s) {
            living.growUp = 100.0;
        } else if (why == u"angry"_s) {
            living.provokedBy = player().id;
            living.angerTimer = 20.0;
        } else if (why == u"busy"_s) {
            living.barter->admiring = QcItemStack{item("qtcraft:clock"), 1, 0};
            living.barter->admireSeconds = 60.0;
        }
        const QcItemStack before = living.barter->admiring;
        const QcServerBarter::Giving giving = m_server->systems().animals.barter().give(player(), tuskling);
        QCOMPARE(giving, QcServerBarter::Giving::Refused);
        send(QcMsg::UseEntity{tuskling});
        QCOMPARE(player().inventory.countOf(item("qtcraft:gold_ingot")), ingots);
        QCOMPARE(mob(tuskling).barter->admiring, before);
    }

    // From the hand it takes its currency only, as in the role model: the armour goes on instead.
    void itTakesOnlyTheCurrencyFromTheHand()
    {
        joinPasture();
        wearGold();
        const quint32 tuskling = spawn(u"tuskling"_s, 1, 0);
        hold("qtcraft:golden_sword");
        QCOMPARE(m_server->systems().animals.barter().give(player(), tuskling), QcServerBarter::Giving::NotTheCurrency);
        send(QcMsg::UseEntity{tuskling});
        QVERIFY(!mob(tuskling).barter->admires());
        QCOMPARE(player().inventory.countOf(item("qtcraft:golden_sword")), 1);
    }

    // Chasing is no reason to refuse; only a blow is. It lets go of its foe for the gold.
    void aFoeInTheChaseMayStillGiveGold()
    {
        joinPasture();
        hold("qtcraft:gold_ingot");
        const quint32 tuskling = spawn(u"tuskling"_s, 1, 0);
        ticks(QcMobSteering::sightCheckTicks + 1);
        QCOMPARE(mob(tuskling).chasing, player().id);
        send(QcMsg::UseEntity{tuskling});
        QVERIFY(mob(tuskling).barter->admires());
        ticks(2);
        QCOMPARE(mob(tuskling).chasing, 0u);
    }

    void goldLyingDrawsItFromItsFoe()
    {
        joinPasture();
        const quint32 tuskling = spawn(u"tuskling"_s, 7, 0);
        ticks(QcMobSteering::sightCheckTicks + 1);
        QCOMPARE(mob(tuskling).chasing, player().id);
        // Five blocks off to the side, so that the way to the player does not pass it.
        lay(QcItemStack{item("qtcraft:gold_ingot"), 1, 0}, 7, 5);
        QTRY_VERIFY_WITH_TIMEOUT((ticks(5), mob(tuskling).barter->admires()), 30000);
        QCOMPARE(player().health, QcServerClient::maxHealth);
    }

    void anAngryOneLeavesGoldLying()
    {
        joinPasture();
        const quint32 tuskling = spawn(u"tuskling"_s, 2, 0);
        QcCreature &living = *m_server->mobs().find(tuskling);
        living.provokedBy = player().id;
        living.angerTimer = 20.0;
        lay(QcItemStack{item("qtcraft:gold_ingot"), 1, 0}, 2, 1);
        ticks(QcServerBarter::seekTicks + 2);
        QVERIFY(!mob(tuskling).barter->admires());
        QCOMPARE(lyingAround().value(u"qtcraft:gold_ingot"_s), 1);
    }

    // A blow ends the looking: the very stack falls, nothing is bartered, nothing is given back.
    void angerWhileItLooksLetsTheItemFall()
    {
        joinPasture();
        wearGold();
        const quint32 tuskling = spawn(u"tuskling"_s, 2, 0);
        hold("qtcraft:gold_ingot");
        player().inventory.setSlot(player().inventory.selectedHotbarSlot(), namedIngot());
        send(QcMsg::UseEntity{tuskling});
        QCOMPARE(mob(tuskling).barter->admiring, namedIngot());
        send(QcMsg::AttackEntity{tuskling});
        ticks(3);
        QVERIFY(!mob(tuskling).barter->admires());
        QCOMPARE(mob(tuskling).provokedBy, player().id);
        QCOMPARE(lyingAround().size(), 1);
        QVERIFY(lies(namedIngot()));
        QCOMPARE(mob(tuskling).barter->hoard.size(), 0);
    }

    // Behind glass it stops trying after a while, takes what it can reach and tries again later.
    void itGivesUpOnWhatItCannotReach()
    {
        joinPasture();
        wearGold();
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dy = 0; dy <= 3 && (std::abs(dx) == 2 || std::abs(dz) == 2 || dy == 3); ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx - 6, dy, dz - 6), id("qtcraft:glass"));
                }
            }
        }
        const quint32 unreachable = lay(QcItemStack{item("qtcraft:gold_ingot"), 1, 0}, -6, -6);
        const quint32 tuskling = spawn(u"tuskling"_s, -2, -6);
        ticks(QcServerBarter::seekTicks + 2);
        QCOMPARE(mob(tuskling).barter->sought, unreachable);
        lay(QcItemStack{item("qtcraft:golden_hoe"), 1, 0}, 3, -6);
        ticks(static_cast<int>(QcServerBarter::seekLimitTicks) + 2 * QcServerBarter::seekTicks);
        QVERIFY(mob(tuskling).barter->ignored.contains(unreachable));
        // It turns to what it can reach instead of standing at the glass.
        QTRY_VERIFY_WITH_TIMEOUT((ticks(5), mob(tuskling).barter->admires()), 30000);
        QCOMPARE(mob(tuskling).barter->admiring.item, item("qtcraft:golden_hoe"));
        ticks(130);
        QTRY_VERIFY_WITH_TIMEOUT((ticks(5), mob(tuskling).barter->sought == unreachable), 30000);
        QVERIFY(!mob(tuskling).barter->ignored.contains(unreachable));
    }

    void everyDeathLetsTheHoardFall_data()
    {
        QTest::addColumn<QString>("death");
        QTest::newRow("a blow") << u"blow"_s;
        QTest::newRow("contact with a campfire") << u"campfire"_s;
        QTest::newRow("suffocation") << u"wall"_s;
    }

    void everyDeathLetsTheHoardFall()
    {
        QFETCH(QString, death);
        joinPasture(u"creative"_s);
        const quint32 tuskling = richTuskling(4, 4);
        QcCreature &living = *m_server->mobs().find(tuskling);
        living.health = 0.5;
        living.noAi = true;
        if (death == u"blow"_s) {
            QVERIFY(m_server->mobs().wound(tuskling, 1000.0).has_value());
        } else if (death == u"campfire"_s) {
            m_server->world().setBlock(m_feet.offset(4, 0, 4), id("qtcraft:campfire"));
        } else {
            buryAt(4, 4);
        }
        QTRY_VERIFY_WITH_TIMEOUT((ticks(5), m_server->mobs().find(tuskling) == nullptr), 30000);
        QVERIFY(lies(treasure()));
        QVERIFY(lies(namedIngot()));
    }

    // Lava burns the hoard like any item that falls into it (decision E-1081).
    void lavaBurnsTheHoard()
    {
        joinPasture(u"creative"_s);
        const quint32 tuskling = richTuskling(4, 4);
        QcCreature &living = *m_server->mobs().find(tuskling);
        living.health = 0.5;
        living.noAi = true;
        m_server->world().setBlock(m_feet.offset(4, 0, 4), id("qtcraft:lava"));
        m_server->world().setBlock(m_feet.offset(4, 1, 4), id("qtcraft:lava"));
        QTRY_VERIFY_WITH_TIMEOUT((ticks(5), m_server->mobs().find(tuskling) == nullptr), 30000);
        ticks(5);
        QVERIFY(lyingAround().isEmpty());
    }

    void aCreatureRemovedLeavesNothing()
    {
        joinPasture(u"creative"_s);
        QVERIFY(m_server->mobs().remove(richTuskling(4, 4)));
        QVERIFY(lyingAround().isEmpty());
    }

    void theBarterStateIsSaved()
    {
        QTemporaryDir dir;
        join(configIn(dir.path()));
        loadAround(m_feet, 2);
        m_server->mobs().setNaturalSpawning(false);
        m_server->mobs().removeAll();
        const quint32 tuskling = spawn(u"tuskling"_s, 1, 0);
        QcCreature &living = *m_server->mobs().find(tuskling);
        m_server->mobs().keep(living, true);
        living.barter->admiring = namedIngot();
        living.barter->admireSeconds = 3.5;
        living.barter->hoard = {treasure(), QcItemStack{item("qtcraft:clock"), 1, 0}};
        QVERIFY(m_server->save().has_value());

        reopen(dir.path());
        loadAround(m_feet, 2);
        const QList<QcCreature> &mobs = m_server->mobs().mobs();
        const auto found = std::ranges::find(mobs, kind(u"tuskling"_s), &QcCreature::type);
        QVERIFY(found != mobs.end());
        QVERIFY(found->barter);
        QCOMPARE(found->barter->admiring, namedIngot());
        QVERIFY(std::abs(found->barter->admireSeconds - 3.5) < 0.2);
        QCOMPARE(found->barter->hoard, (QList<QcItemStack>{treasure(), QcItemStack{item("qtcraft:clock"), 1, 0}}));
    }

    // A gift goes through a save and a reload like any other state: the barter ends there, once.
    void aGiftSurvivesASave()
    {
        QTemporaryDir dir;
        join(configIn(dir.path()));
        loadAround(m_feet, 2);
        chat(u"/gamemode survival"_s);
        m_server->mobs().setNaturalSpawning(false);
        m_server->mobs().removeAll();
        floorOf("qtcraft:grass_block", 10);
        wearGold();
        const quint32 tuskling = spawn(u"tuskling"_s, 1, 0);
        hold("qtcraft:gold_ingot");
        send(QcMsg::UseEntity{tuskling});
        QVERIFY(mob(tuskling).barter->admires());
        QVERIFY(m_server->save().has_value());

        reopen(dir.path());
        loadAround(m_feet, 2);
        wearGold();
        const QList<QcCreature> &mobs = m_server->mobs().mobs();
        const auto found = std::ranges::find(mobs, kind(u"tuskling"_s), &QcCreature::type);
        QVERIFY(found != mobs.end() && found->barter->admires());
        const quint32 again = found->id;
        ticks(125);
        QVERIFY(!mob(again).barter->admires());
        QVERIFY2(!lyingAround().isEmpty(), "the barter ended without loot");
        QVERIFY(!lyingAround().contains(u"qtcraft:gold_ingot"_s));
    }

    void aHoardGoesThroughAPortal()
    {
        joinPasture(u"creative"_s);
        const quint32 tuskling = richTuskling(4, 4);
        m_server->mobs().keep(*m_server->mobs().find(tuskling), true);
        QcResult<QcServerDimension *> nether = m_server->openDimension(u"qtcraft:nether"_s);
        QVERIFY(nether);
        (*nether)->world().loadChunk({0, 0});
        QcResult<QcServerDimension *> overworld = m_server->openDimension(QString::fromLatin1(QcDimensionDefinition::overworldName));
        QVERIFY(overworld);
        QVERIFY(QcEntityTravel::move({QcEntityKind::Creature, tuskling, *overworld, *nether, {8.5, 70.0, 8.5}}));
        QcMobManager &there = (*nether)->entities().creatures();
        QVERIFY(m_server->mobs().find(tuskling) == nullptr);
        QVERIFY(there.find(tuskling) != nullptr);
        QCOMPARE(there.find(tuskling)->barter->hoard, QList<QcItemStack>{treasure()});
        QCOMPARE(there.find(tuskling)->barter->admiring, namedIngot());
        // It dies there, and there is where the hoard falls.
        QVERIFY(there.wound(tuskling, 1000.0).has_value());
        QVERIFY(lyingAround().isEmpty());
        QCOMPARE((*nether)->entities().items().items().size(), 2);
    }

    // The whole server tick with QcServerBarter's looks for items (decision E-1086): 100 stacks
    // no creature takes lie around, so every look scans them all. Measures, asserts only the crowd.
    void tickCostOfBartering()
    {
        joinPasture(u"creative"_s);
        wearGold();
        chat(u"/time set night"_s);
        for (int i = 0; i < 100; ++i) {
            const int row = i / 10;
            const QcVec3 at{m_feet.x + (i % 10) * 2.0 - 8.5, m_feet.y + 0.1, m_feet.z + row * 2.0 - 8.5};
            m_server->entities().items().spawn({.stack = QcItemStack{item("qtcraft:stone"), 1, 0}, .position = at, .velocity = {}}, m_server->entities().newId());
        }
        const auto cost = [this](const QString &name) {
            m_server->mobs().removeAll();
            for (int i = 0; i < 40; ++i) {
                const int row = i / 8;
                spawn(name, (i % 8) * 2.0 - 7.0, row * 2.0 - 4.0);
            }
            ticks(100);
            QElapsedTimer timer;
            timer.start();
            ticks(300);
            return static_cast<double>(timer.nsecsElapsed()) / 300 / 1e6;
        };
        const double zombies = cost(u"zombie"_s);
        const double tusklings = cost(u"tuskling"_s);
        qInfo().noquote() << u"ms per server tick with 100 stacks lying: 40 zombies %1, 40 tusklings %2"_s.arg(zombies, 0, 'f', 3).arg(tusklings, 0, 'f', 3);
        const auto crowd = std::ranges::count_if(m_server->mobs().mobs(), [this](const QcCreature &mob) { return mob.type == kind(u"tuskling"_s); });
        QCOMPARE(crowd, 40);
    }
};

QTEST_GUILESS_MAIN(TestBarter)
#include "test_barter.moc"
