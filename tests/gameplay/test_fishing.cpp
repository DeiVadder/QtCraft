// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "server/core/QcServerSystems.h"
#include "server/projectiles/QcPlayerFishing.h"
#include "server/storage/QcEntityRecords.h"
#include "server/weather/QcServerWeather.h"
#include "server/weather/QcWeatherCycle.h"

#include <QHash>

using namespace Qt::StringLiterals;

// Fishing (decision E-503): the rod casts a bobber that floats on water, a fish bites after a random
// wait, reeling in during the bite catches one entry of the loot table by weight and wears the rod;
// the bobber goes with another slot, distance or the player, and is never saved. Ticks are stepped
// by hand.
class TestFishing : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcServerClient &player() { return *m_server->entities().clients().front(); }
    const QcItemDefinition &rodItem() const { return m_content->item(item("qtcraft:fishing_rod")); }
    const QcFishingDefinition &rod() const { return *rodItem().fishing; } // NOLINT(bugprone-unchecked-optional-access): checked in rodComesFromData

    const QcProjectile *bobber()
    {
        const QList<QcProjectile> &all = m_server->entities().projectiles().all();
        const auto it = std::ranges::find(all, player().fishing.bobber, &QcProjectile::id);
        return player().fishing.bobber != 0 && it != all.end() ? &*it : nullptr;
    }

    qsizetype projectiles() const { return m_server->entities().projectiles().all().size(); }

    // A stone basin of water (or `liquid`) 9 x 9 and `depth` deep east of the player: x 3..11,
    // z -4..4, its surface at the feet's level.
    void buildPool(const char *liquid = "qtcraft:water", int depth = 3)
    {
        for (int dx = 2; dx <= 12; ++dx) {
            for (int dz = -5; dz <= 5; ++dz) {
                const bool wall = dx == 2 || dx == 12 || dz == -5 || dz == 5;
                for (int dy = -4; dy <= 3; ++dy) {
                    const bool water = !wall && dy >= -depth && dy <= -1;
                    const bool floor = dy < -depth || (wall && dy <= -1);
                    const QcBlockId block = floor ? id("qtcraft:stone") : (water ? id(liquid) : qcAirBlockId);
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), block);
                }
            }
        }
    }

    void joinWithRod()
    {
        join();
        chat(u"/gamemode survival"_s);
        chat(u"/give fishing_rod 1"_s);
        chat(u"/give stick 1"_s);
        buildPool();
        hold("qtcraft:fishing_rod");
    }

    void hold(const char *name)
    {
        const int slot = slotOf(item(name));
        QVERIFY2(slot >= 0 && slot < 9, name);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    // Casts east and a little down, into the pool, and ticks until the bobber floats.
    void castIntoPool()
    {
        player().yaw = 90.0f;
        player().pitch = -20.0f;
        send(QcMsg::ThrowItem{});
        QVERIFY(bobber());
        for (int i = 0; i < 100 && !bobber()->motion.inWater; ++i) {
            ticks(1);
        }
        QVERIFY(bobber()->motion.inWater);
    }

    // Ticks until a fish bites (at most the longest wait).
    void waitForBite()
    {
        const int limit = static_cast<int>(rod().maxSeconds / QcPhysics::tickDuration) + 40;
        for (int i = 0; i < limit && player().fishing.biteTicks == 0; ++i) {
            ticks(1);
        }
        QVERIFY(player().fishing.biteTicks > 0);
    }

    int rodDamage() { return player().inventory.slot(player().inventory.selectedHotbarSlot()).damage; }

    QList<QcItemId> lootItems() const
    {
        QList<QcItemId> items;
        for (const QcFishingLoot &entry : rod().loot) {
            items.append(*m_content->itemId(entry.item));
        }
        return items;
    }

    qsizetype caughtEntities() const
    {
        const QList<QcItemId> loot = lootItems();
        return std::ranges::count_if(m_server->entities().items().items(), [&](const auto &entity) { return loot.contains(entity.stack.item); });
    }

private slots:
    void rodComesFromData()
    {
        const QcItemDefinition &definition = rodItem();
        QVERIFY(definition.fishing.has_value());
        QCOMPARE(definition.durability, 64);
        QCOMPARE(definition.maxStack, 1);
        const QcProjectileDefinition *bobber = m_content->projectile(rod().bobber);
        QVERIFY(bobber);
        QVERIFY(bobber->floats);
        QVERIFY(bobber->onHit.isEmpty());
        QVERIFY(rod().minSeconds < rod().maxSeconds);
        QVERIFY(!rod().loot.isEmpty());
        for (const QcFishingLoot &entry : rod().loot) {
            QVERIFY2(m_content->itemId(entry.item), qPrintable(entry.item));
        }
        QVERIFY(std::ranges::any_of(m_content->recipes(), [](const QcRecipeDefinition &recipe) { return recipe.resultItem == u"qtcraft:fishing_rod"_s; }));
    }

    void waitIsSeededAndShorterInRain()
    {
        QRandomGenerator a(4711);
        QRandomGenerator b(4711);
        QRandomGenerator rain(4711);
        const int low = static_cast<int>(std::lround(rod().minSeconds / QcPhysics::tickDuration));
        const int high = static_cast<int>(std::lround(rod().maxSeconds / QcPhysics::tickDuration));
        for (int i = 0; i < 200; ++i) {
            const int dry = QcPlayerFishing::waitTicks(rod(), false, a);
            QCOMPARE(QcPlayerFishing::waitTicks(rod(), false, b), dry);
            QVERIFY(dry >= low && dry <= high);
            const int wet = QcPlayerFishing::waitTicks(rod(), true, rain);
            QVERIFY(std::abs(wet - dry * rod().rainFactor) <= 1.0);
        }
    }

    void lootFollowsTheWeights()
    {
        QRandomGenerator random(99);
        QHash<QString, int> drawn;
        constexpr int rolls = 20000;
        for (int i = 0; i < rolls; ++i) {
            const QcFishingLoot *entry = QcPlayerFishing::draw(rod(), random);
            QVERIFY(entry);
            ++drawn[entry->item];
        }

        int total = 0;
        for (const QcFishingLoot &entry : rod().loot) {
            total += entry.weight;
        }
        for (const QcFishingLoot &entry : rod().loot) {
            const double expected = static_cast<double>(entry.weight) / total;
            const double share = static_cast<double>(drawn.value(entry.item)) / rolls;
            QVERIFY2(std::abs(share - expected) < 0.015, qPrintable(u"%1: %2 instead of %3"_s.arg(entry.item).arg(share).arg(expected)));
        }

        QcFishingDefinition empty;
        QCOMPARE(QcPlayerFishing::draw(empty, random), nullptr);
    }

    void bobberFloatsOnTheSurface()
    {
        joinWithRod();
        castIntoPool();
        ticks(40);
        const QcProjectile *floating = bobber();
        QVERIFY(floating);
        QVERIFY(floating->motion.inWater);
        QVERIFY(!floating->stuck);
        // At rest just below the top of the uppermost water block, inside the pool.
        QVERIFY(std::abs(floating->motion.position.y - (m_feet.y - 1 + QcProjectileFlight::floatHeight)) < 1e-6);
        QVERIFY(floating->motion.velocity.length() < 1e-6);
        QVERIFY(floating->motion.position.x > m_feet.x + 3 && floating->motion.position.x < m_feet.x + 12);
        QCOMPARE(floating->pickup, QcProjectilePickup::Never);
    }

    void biteComesAfterTheDrawnWait()
    {
        joinWithRod();
        castIntoPool();
        // The wait is drawn in the first tick the bobber floats; it counts down by one per tick.
        for (int i = 0; i < 5 && player().fishing.waitTicks < 0; ++i) {
            ticks(1);
        }
        const int left = player().fishing.waitTicks;
        QVERIFY(left > 0);
        QVERIFY(left <= int(rod().maxSeconds / QcPhysics::tickDuration));
        const double surface = m_feet.y - 1 + QcProjectileFlight::floatHeight;
        ticks(left - 1);
        QCOMPARE(player().fishing.biteTicks, 0);
        ticks(1);
        QCOMPARE(player().fishing.biteTicks, int(std::lround(rod().biteSeconds / QcPhysics::tickDuration)));
        // The bobber dips and floats up again.
        QVERIFY(bobber()->motion.position.y < surface - 0.1);
        ticks(10);
        QVERIFY(std::abs(bobber()->motion.position.y - surface) < 1e-6);

        // Missed: after the bite a new wait begins.
        ticks(player().fishing.biteTicks);
        QCOMPARE(player().fishing.biteTicks, 0);
        ticks(1);
        QVERIFY(player().fishing.waitTicks > 0);
    }

    void reelingDuringTheBiteCatches()
    {
        joinWithRod();
        castIntoPool();
        waitForBite();
        QCOMPARE(rodDamage(), 0);
        send(QcMsg::ThrowItem{});
        QCOMPARE(player().fishing.bobber, quint32(0));
        QCOMPARE(projectiles(), 0);
        QCOMPARE(caughtEntities(), 1);
        QCOMPARE(rodDamage(), 1);

        // The catch flies to the player and is picked up.
        int carried = 0;
        for (int i = 0; i < 80 && carried == 0; ++i) {
            ticks(1);
            const QList<QcItemId> loot = lootItems();
            for (const QcItemId caught : loot) {
                carried += countOf(caught) - (caught == item("qtcraft:stick") ? 1 : 0);
            }
        }
        QVERIFY(carried > 0);
    }

    void reelingWithoutABiteCatchesNothing()
    {
        joinWithRod();
        castIntoPool();
        ticks(2);
        QCOMPARE(player().fishing.biteTicks, 0);
        send(QcMsg::ThrowItem{});
        QCOMPARE(player().fishing.bobber, quint32(0));
        QCOMPARE(projectiles(), 0);
        QCOMPARE(caughtEntities(), 0);
        QCOMPARE(rodDamage(), 0);
    }

    void rodWearsOutAndBreaks()
    {
        joinWithRod();
        const int slot = player().inventory.selectedHotbarSlot();
        player().inventory.setSlot(slot, {item("qtcraft:fishing_rod"), 1, rodItem().durability - 1});
        castIntoPool();
        waitForBite();
        send(QcMsg::ThrowItem{});
        QVERIFY(player().inventory.slot(slot).isEmpty());
    }

    void bobberGoesWithAnotherSlot()
    {
        joinWithRod();
        castIntoPool();
        hold("qtcraft:stick");
        ticks(1);
        QCOMPARE(player().fishing.bobber, quint32(0));
        QCOMPARE(projectiles(), 0);
    }

    void bobberGoesWhenThePlayerIsFarAway()
    {
        joinWithRod();
        castIntoPool();
        player().motion.position.x -= rod().maxDistance + 2.0;
        ticks(1);
        QCOMPARE(player().fishing.bobber, quint32(0));
        QCOMPARE(projectiles(), 0);
    }

    void bobberIsNeverSaved()
    {
        joinWithRod();
        castIntoPool();
        QCOMPARE(projectiles(), 1);
        const QHash<QcChunkPos, QcEntityRecord> records = QcEntityRecords::collect(m_server->entities());
        for (const QcEntityRecord &record : records) {
            QVERIFY(record.projectiles.isEmpty());
        }

        // A bobber nobody fishes with any more (its player left) goes in the next tick.
        player().fishing = {};
        ticks(1);
        QCOMPARE(projectiles(), 0);
    }

    void bobberHarmsNoCreature()
    {
        joinWithRod();
        const int cow = *m_content->mobIndex(u"qtcraft:cow"_s);
        const QcVec3 at{m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z - 2.5};
        const quint32 id = m_server->entities().creatures().spawn(cow, at, m_server->entities().newId(), false, true);
        const double health = m_server->entities().creatures().find(id)->health;
        player().yaw = 0.0f;
        player().pitch = -5.0f;
        send(QcMsg::ThrowItem{});

        // It flies north at the cow and bounces back off it: its speed turns south, and it never
        // passes the cow.
        bool bounced = false;
        for (int i = 0; i < 20 && bobber(); ++i) {
            bounced = bounced || bobber()->motion.velocity.z > 0.0;
            QVERIFY(bobber()->motion.position.z > at.z - 0.5);
            ticks(1);
        }
        QVERIFY(bounced);
        QCOMPARE(m_server->entities().creatures().find(id)->health, health);
        QVERIFY(!m_server->entities().creatures().find(id)->hurt.active());
    }

    void lavaBurnsTheBobberUp()
    {
        join();
        chat(u"/gamemode survival"_s);
        chat(u"/give fishing_rod 1"_s);
        buildPool("qtcraft:lava");
        hold("qtcraft:fishing_rod");
        player().yaw = 90.0f;
        player().pitch = -20.0f;
        send(QcMsg::ThrowItem{});
        QVERIFY(bobber());
        for (int i = 0; i < 100 && player().fishing.bobber != 0; ++i) {
            ticks(1);
            QVERIFY(!bobber() || !bobber()->motion.inWater);
        }
        // Gone without a bite and without wearing the rod.
        QCOMPARE(player().fishing.bobber, quint32(0));
        QCOMPARE(projectiles(), 0);
        QCOMPARE(rodDamage(), 0);
        QCOMPARE(caughtEntities(), 0);
    }

    void aFastBobberLandsInShallowWater()
    {
        join();
        chat(u"/give fishing_rod 1"_s);
        buildPool("qtcraft:water", 1);
        hold("qtcraft:fishing_rod");
        // From high above, steeply down: a step of the flight is longer than the water is deep.
        player().motion.position.y += 6.0;
        player().yaw = 90.0f;
        player().pitch = -65.0f;
        send(QcMsg::ThrowItem{});
        for (int i = 0; i < 40 && bobber() && !bobber()->motion.inWater && !bobber()->stuck; ++i) {
            ticks(1);
        }
        QVERIFY(bobber());
        QVERIFY(!bobber()->stuck);
        ticks(20);
        QVERIFY(bobber()->motion.inWater);
        QVERIFY(std::abs(bobber()->motion.position.y - (m_feet.y - 1 + QcProjectileFlight::floatHeight)) < 1e-6);
    }

    void castAndReelWaitForTheCooldown()
    {
        joinWithRod();
        player().yaw = 90.0f;
        player().pitch = -20.0f;
        send(QcMsg::ThrowItem{});
        const quint32 first = player().fishing.bobber;
        QVERIFY(first != 0);
        // A second press within the cooldown neither reels in nor swings.
        player().swung = false;
        send(QcMsg::ThrowItem{});
        QCOMPARE(player().fishing.bobber, first);
        ticks(QcPlayerFishing::useCooldownTicks);
        send(QcMsg::ThrowItem{});
        QCOMPARE(player().fishing.bobber, quint32(0));
    }

    void bobberGoesWhenThePlayerDies()
    {
        joinWithRod();
        castIntoPool();
        chat(u"/kill"_s);
        ticks(1);
        QCOMPARE(projectiles(), 0);
        QCOMPARE(player().fishing.bobber, quint32(0));
    }

    void bobberGoesWhenThePlayerLeaves()
    {
        joinWithRod();
        castIntoPool();
        m_client.reset();
        QcTestUtil::pump();
        ticks(2);
        QVERIFY(m_server->entities().clients().empty());
        QCOMPARE(projectiles(), 0);
    }

    void bobberGoesWithItsChunk()
    {
        joinWithRod();
        castIntoPool();
        const QcChunkPos chunk = QcEntities::chunkOf(*bobber());
        const QHash<QcChunkPos, QcEntityRecord> records = QcEntityRecords::take(m_server->entities(), {chunk});
        QVERIFY(records.value(chunk).projectiles.isEmpty());
        QCOMPARE(projectiles(), 0);
        // The player forgets it in the next tick and casts anew.
        ticks(QcPlayerFishing::useCooldownTicks);
        QCOMPARE(player().fishing.bobber, quint32(0));
        send(QcMsg::ThrowItem{});
        QVERIFY(bobber());
    }

    void rainOnTheWaterComesFromTheServer()
    {
        joinWithRod();
        const QcBlockPos water = m_feet.offset(6, -1, 0);
        const QcPlayerFishing &fishing = m_server->systems().projectiles.fishing();
        chat(u"/gamerule weatherCycle false"_s);
        chat(u"/weather clear"_s);
        ticks(QcWeatherCycle::fadeTicks + 2);
        QVERIFY(!fishing.rainsAt(water));
        chat(u"/weather rain"_s);
        ticks(QcWeatherCycle::fadeTicks + 2);
        QCOMPARE(fishing.rainsAt(water), m_server->systems().weather.isRainingAt(water.offset(0, 1, 0)));
        QVERIFY(fishing.rainsAt(water));
        // Under a roof the water stays dry.
        m_server->world().setBlock(water.offset(0, 4, 0), id("qtcraft:stone"));
        QVERIFY(!fishing.rainsAt(water));
    }

    void twoAnglersFishOnOnePond()
    {
        joinWithRod();
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        auto other = std::make_unique<RawClient>(std::move(clientEnd));
        other->hello(u"Other"_s);
        QcTestUtil::pump();
        ticks(2);
        QcServerClient &angler = *m_server->entities().clients().back();
        QVERIFY(&angler != &player());
        angler.inventory.setSlot(0, {item("qtcraft:fishing_rod"), 1, 0});
        other->connection.send(QcMsg::SelectHotbarSlot{0});
        angler.motion.position = player().motion.position + QcVec3{0.0, 0.0, 2.0};
        angler.yaw = 90.0f;
        angler.pitch = -20.0f;
        other->connection.send(QcMsg::ThrowItem{});
        QcTestUtil::pump();
        castIntoPool();
        ticks(5);

        // Each has a bobber of his own, both floating; reeling one in leaves the other.
        QVERIFY(angler.fishing.bobber != 0 && player().fishing.bobber != 0);
        QVERIFY(angler.fishing.bobber != player().fishing.bobber);
        QCOMPARE(projectiles(), 2);
        send(QcMsg::ThrowItem{});
        QCOMPARE(projectiles(), 1);
        QVERIFY(angler.fishing.bobber != 0);
        const auto &all = m_server->entities().projectiles().all();
        QVERIFY(std::ranges::find(all, angler.fishing.bobber, &QcProjectile::id) != all.end());
    }
};

QTEST_GUILESS_MAIN(TestFishing)
#include "test_fishing.moc"
