#include "common/physics/QcPhysics.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"
#include "server/items/QcItemEntityManager.h"
#include "server/plants/QcPlantManager.h"
#include "worldgen/terrain/QcTreeShape.h"

#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr double dt = QcPhysics::tickDuration;

int ticksFor(double seconds)
{
    return int(std::ceil(seconds / dt));
}

} // namespace

// Dropped items (falling, pickup, merging, despawn, limits) and plants (leaf decay, saplings).
class TestItems : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcItemId item(const char *name) const { return *m_content->itemId(QString::fromLatin1(name)); }
    QcBlockId block(const char *name) const { return *m_content->blockId(QString::fromLatin1(name)); }

    // A stone floor whose top is at y = 64.
    QcTestWorld floorWorld() const
    {
        QcTestWorld world(*m_content);
        world.fillBox({-8, 63, -8}, {8, 63, 8}, block("qtcraft:stone"));
        return world;
    }

    static QcItemEntityManager::Drop dropAt(QcItemStack stack, const QcVec3 &position, double pickupDelay = 0.0)
    {
        QcItemEntityManager::Drop drop;
        drop.stack = stack;
        drop.position = position;
        drop.pickupDelay = pickupDelay;
        return drop;
    }

    // Items where `isLoaded` says no rest, as the server's entity register decides at the start of
    // each tick (QcEntities::settle).
    static void settle(QcItemEntityManager &items, const QcItemEntityManager::LoadedFunction &isLoaded)
    {
        for (QcItemEntity &entry : items.editableItems()) {
            entry.dormant = !isLoaded(entry.motion.position);
        }
    }

    // One tick after settling the items like the server.
    static QList<QcItemEntity> tickOnce(QcItemEntityManager &items, const QcTestWorld &world,
                                        const QList<QcItemEntityManager::PlayerView> &players,
                                        const QcItemEntityManager::TickCallbacks &callbacks)
    {
        settle(items, callbacks.isLoaded);
        return items.tick(world, players, dt, callbacks);
    }

    // Runs ticks with `players` in the world and the given callbacks; new entity ids count up from 1000.
    static QList<QcItemEntity> tickTimes(QcItemEntityManager &items, const QcTestWorld &world, int count,
                                                      const QList<QcItemEntityManager::PlayerView> &players,
                                                      QcItemEntityManager::TickCallbacks callbacks)
    {
        quint32 nextId = 1000;
        callbacks.nextEntityId = [&nextId] { return nextId++; };
        QList<QcItemEntity> despawned;
        for (int i = 0; i < count; ++i) {
            despawned += tickOnce(items, world, players, callbacks);
        }
        return despawned;
    }

    // Runs ticks without players; everything counts as loaded unless `loaded` is false.
    static QList<QcItemEntity> run(QcItemEntityManager &items, const QcTestWorld &world, int count, bool loaded = true)
    {
        return tickTimes(items, world, count, {},
                         {[loaded](const QcVec3 &) { return loaded; }, [](quint32, QcItemStack stack) { return stack.count; }, {}});
    }

    // Runs ticks with `players` in the loaded world; `give` hands them the items they touch.
    static QList<QcItemEntity> runWithPlayers(QcItemEntityManager &items, const QcTestWorld &world, int count,
                                                           const QList<QcItemEntityManager::PlayerView> &players,
                                                           const QcItemEntityManager::GiveFunction &give)
    {
        return tickTimes(items, world, count, players, {[](const QcVec3 &) { return true; }, give, {}});
    }

private slots:
    void itemsFallAndComeToRest()
    {
        const QcTestWorld world = floorWorld();
        QcItemEntityManager items(m_content, 1);
        items.spawn(dropAt({item("qtcraft:dirt"), 1, 0}, {0.5, 70.0, 0.5}), 1);
        run(items, world, 60);
        QCOMPARE(items.items().size(), 1);
        const QcItemEntity &dropped = items.items().first();
        QCOMPARE(dropped.motion.position.y, 64.0);
        QVERIFY(dropped.motion.onGround);
        QVERIFY(dropped.resting);
    }

    void restingItemsFallWhenTheGroundGoes()
    {
        QcTestWorld world = floorWorld();
        QcItemEntityManager items(m_content, 1);
        items.spawn(dropAt({item("qtcraft:dirt"), 1, 0}, {0.5, 64.0, 0.5}), 1);
        run(items, world, 5);
        QVERIFY(items.items().first().resting);
        world.set({0, 63, 0}, qcAirBlockId);
        world.set({0, 60, 0}, block("qtcraft:stone"));
        run(items, world, 40);
        QCOMPARE(items.items().first().motion.position.y, 61.0);
    }

    void itemsFloatUpInWater()
    {
        QcTestWorld world = floorWorld();
        world.fillBox({-2, 64, -2}, {2, 67, 2}, block("qtcraft:water"));
        QcItemEntityManager items(m_content, 1);
        items.spawn(dropAt({item("qtcraft:dirt"), 1, 0}, {0.5, 64.5, 0.5}), 1);
        run(items, world, 100);
        // Floating at the surface: the top water block reaches y = 68.
        QVERIFY2(items.items().first().motion.position.y > 67.0, qPrintable(QString::number(items.items().first().motion.position.y)));
    }

    void pickupWaitsForTheDelayAndTakesWhatFits()
    {
        const QcTestWorld world = floorWorld();
        QcItemEntityManager items(m_content, 1);
        items.spawn(dropAt({item("qtcraft:dirt"), 10, 0}, {0.5, 64.0, 0.5}, 0.5), 1);
        const QList<QcItemEntityManager::PlayerView> players = {{7, {0.5, 64.0, 0.5}, true}};

        int given = 0;
        // The player has room for 4 items only.
        const auto give = [&given](quint32 player, QcItemStack stack) {
            if (player != 7) {
                return stack.count;
            }
            const int taken = std::min(stack.count, 4 - given);
            given += taken;
            return stack.count - taken;
        };

        runWithPlayers(items, world, ticksFor(0.5) - 2, players, give);
        QCOMPARE(given, 0);
        runWithPlayers(items, world, 3, players, give);
        QCOMPARE(given, 4);
        QCOMPARE(items.items().size(), 1);
        QCOMPARE(items.items().first().stack.count, 6);
    }

    void deadOrDistantPlayersPickUpNothing()
    {
        const QcTestWorld world = floorWorld();
        QcItemEntityManager items(m_content, 1);
        items.spawn(dropAt({item("qtcraft:dirt"), 3, 0}, {0.5, 64.0, 0.5}), 1);
        int given = 0;
        const auto give = [&given](quint32, QcItemStack stack) {
            given += stack.count;
            return 0;
        };

        runWithPlayers(items, world, 5, {{1, {0.5, 64.0, 0.5}, false}, {2, {2.5, 64.0, 0.5}, true}}, give);
        QCOMPARE(given, 0);

        // An item falling onto a player's head counts as touching.
        items.spawn(dropAt({item("qtcraft:dirt"), 2, 0}, {5.5, 66.2, 0.5}), 2);
        runWithPlayers(items, world, 2, {{3, {5.5, 64.0, 0.5}, true}}, give);
        QCOMPARE(given, 2);
    }

    void equalStacksNearbyMerge()
    {
        const QcTestWorld world = floorWorld();
        QcItemEntityManager items(m_content, 1);
        items.spawn(dropAt({item("qtcraft:dirt"), 40, 0}, {0.5, 64.0, 0.5}), 1);
        items.spawn(dropAt({item("qtcraft:dirt"), 40, 0}, {1.2, 64.0, 0.5}), 2);
        items.spawn(dropAt({item("qtcraft:dirt"), 5, 0}, {6.5, 64.0, 0.5}), 3);
        items.spawn(dropAt({item("qtcraft:stone"), 5, 0}, {0.6, 64.0, 0.5}), 4);

        const QcItemId pickaxe = item("qtcraft:stone_pickaxe");
        items.spawn(dropAt({pickaxe, 1, 3}, {-3.5, 64.0, 0.5}), 5);
        items.spawn(dropAt({pickaxe, 1, 0}, {-3.4, 64.0, 0.5}), 6);

        run(items, world, QcItemEntityManager::mergeIntervalTicks);

        QList<int> dirt;
        int stone = 0;
        int pickaxes = 0;
        for (const QcItemEntity &entry : items.items()) {
            if (entry.stack.item == item("qtcraft:dirt")) {
                dirt.append(entry.stack.count);
            }
            stone += entry.stack.item == item("qtcraft:stone") ? 1 : 0;
            pickaxes += entry.stack.item == pickaxe ? 1 : 0;
        }

        std::ranges::sort(dirt);
        // 40 + 40 fill one stack of 64; the far stack stays apart; other items and tools stay single.
        QCOMPARE(dirt, (QList<int>{5, 16, 64}));
        QCOMPARE(stone, 1);
        QCOMPARE(pickaxes, 2);
    }

    void itemsAgeOnlyInLoadedChunks()
    {
        const QcTestWorld world = floorWorld();
        QcItemEntityManager items(m_content, 1);
        QcItemEntityManager::Drop old = dropAt({item("qtcraft:dirt"), 1, 0}, {0.5, 64.0, 0.5});
        old.age = QcItemEntityManager::despawnSeconds - 1.0;
        items.spawn(old, 1);
        QVERIFY(run(items, world, ticksFor(5.0), false).isEmpty());
        QCOMPARE(items.items().size(), 1);
        QCOMPARE(items.items().first().age, QcItemEntityManager::despawnSeconds - 1.0);
        const QList<QcItemEntity> despawned = run(items, world, ticksFor(1.0) + 1);
        QCOMPARE(despawned.size(), 1);
        QCOMPARE(despawned.first().stack.item, item("qtcraft:dirt"));
        QVERIFY(items.items().isEmpty());
    }

    void dropsWaitInAQueueAboveTheLimit()
    {
        const QcTestWorld world = floorWorld();
        QcItemEntityManager items(m_content, 1);
        // Different items far apart so nothing merges.
        const QList<QcItemId> kinds = {item("qtcraft:dirt"), item("qtcraft:stone"), item("qtcraft:cobblestone")};
        for (int i = 0; i < QcItemEntityManager::activeLimit + 6; ++i) {
            // A 16 x 16 grid of spots, one layer of 256 above the other.
            const int column = i % 16;
            const int row = (i / 16) % 16;
            const int layer = i / 256;
            const QcVec3 position{column - 8.0 + 0.5, 64.0 + layer * 3.0, row - 8.0 + 0.5};
            items.spawn(dropAt({kinds[i % 3], 1, 0}, position), quint32(i + 1));
        }

        QCOMPARE(items.items().size(), QcItemEntityManager::activeLimit);
        QVERIFY(!items.queued().isEmpty());
        const qsizetype waiting = items.queued().size();

        // A player picking everything up makes room for the waiting drops.
        const auto give = [](quint32, QcItemStack) { return 0; };
        runWithPlayers(items, world, 1, {{1, {0.5, 64.0, 0.5}, true}}, give);
        QVERIFY(items.queued().size() < waiting);
        QVERIFY(items.items().size() <= QcItemEntityManager::activeLimit);
    }

    void theQueueIsBounded()
    {
        QcItemEntityManager items(m_content, 1);
        for (int i = 0; i < QcItemEntityManager::activeLimit; ++i) {
            items.spawn(dropAt({item("qtcraft:dirt"), 1, 0}, {0.5, 64.0, 0.5}), quint32(i + 1));
        }

        // Each waiting drop lies far from the others, so they cannot be combined.
        for (int i = 0; i < QcItemEntityManager::queueLimit + 10; ++i) {
            items.spawn(dropAt({item("qtcraft:stone"), 1, 0}, {double(i) * 3.0, 64.0, 0.5}), 0);
        }
        QCOMPARE(items.queued().size(), QcItemEntityManager::queueLimit);
        QCOMPARE(items.discardedCount(), 10);

        // Close equal drops waiting in the queue combine into one entry.
        QcItemEntityManager combined(m_content, 1);
        for (int i = 0; i < QcItemEntityManager::activeLimit; ++i) {
            combined.spawn(dropAt({item("qtcraft:dirt"), 1, 0}, {0.5, 64.0, 0.5}), quint32(i + 1));
        }

        combined.spawn(dropAt({item("qtcraft:stone"), 3, 0}, {0.5, 64.0, 0.5}), 0);
        combined.spawn(dropAt({item("qtcraft:stone"), 4, 0}, {0.7, 64.0, 0.5}), 0);
        QCOMPARE(combined.queued().size(), 1);
        QCOMPARE(combined.queued().first().stack.count, 7);
    }

    void restoreKeepsAtMostTheTotalLimit()
    {
        const QcTestWorld world = floorWorld();
        QcItemEntityManager items(m_content, 1);
        QList<QcItemEntityManager::Drop> drops;
        for (int i = 0; i < QcItemEntityManager::totalLimit + 100; ++i) {
            drops.append(dropAt({item("qtcraft:dirt"), 1, 0}, {double(i) * 2.0, 64.0, 0.5}));
        }
        drops.append(dropAt({qcNoItem, 1, 0}, {0.5, 64.0, 0.5}));

        quint32 nextId = 1;
        items.restore(drops, [&nextId] { return nextId++; });
        QCOMPARE(items.items().size(), QcItemEntityManager::totalLimit);
        QCOMPARE(items.queued().size(), 100);

        // Nothing is loaded: the saved items stay, the waiting drops keep waiting.
        run(items, world, 1, false);
        QCOMPARE(items.queued().size(), 100);
        QCOMPARE(items.items().size(), QcItemEntityManager::totalLimit);
        QCOMPARE(items.activeCount(), 0);
        QCOMPARE(items.discardedCount(), 0);
    }

    void surplusItemsSleepAndKeepTheirIds()
    {
        const QcTestWorld world = floorWorld();
        QcItemEntityManager items(m_content, 1);
        QList<QcItemEntityManager::Drop> drops;
        for (int i = 0; i < 2000; ++i) {
            QcItemEntityManager::Drop drop = dropAt({item("qtcraft:dirt"), 1, 0}, {double(i) * 3.0, 64.0, 0.5});
            drop.age = double(i % 100);
            drops.append(drop);
        }
        quint32 nextId = 1;
        items.restore(drops, [&nextId] { return nextId++; });
        const auto ids = [&items] {
            QSet<quint32> result;
            for (const QcItemEntity &entry : items.items()) {
                result.insert(entry.id);
            }
            return result;
        };
        const QSet<quint32> before = ids();
        const auto surplus = [&items] {
            return std::ranges::count_if(items.items(), &QcItemEntity::surplus);
        };

        // All chunks load: every item is kept, the youngest beyond the limit sleep.
        run(items, world, 1);
        QCOMPARE(items.items().size(), 2000);
        QVERIFY(items.queued().isEmpty());
        QCOMPARE(items.activeCount(), QcItemEntityManager::activeLimit);
        QCOMPARE(surplus(), 2000 - QcItemEntityManager::activeLimit);

        double oldestSleeper = 0.0;
        double youngestAwake = 1e9;
        // Items a few seconds old count as fresh drops and stay awake; among the others the
        // youngest sleep.
        for (const QcItemEntity &entry : items.items()) {
            if (entry.surplus) {
                QVERIFY(entry.age >= 10.0);
                oldestSleeper = std::max(oldestSleeper, entry.age);
            } else if (entry.age >= 10.0) {
                youngestAwake = std::min(youngestAwake, entry.age);
            }
        }
        QVERIFY(oldestSleeper <= youngestAwake);

        // Only the first 300 items stay loaded: all of them are awake now; nothing moved or vanished.
        const auto firstLoaded = [](const QcVec3 &position) { return position.x < 900.0; };
        const auto keep = [](quint32, QcItemStack stack) { return stack.count; };
        const auto noId = [] { return quint32(0); };
        for (int i = 0; i < 3; ++i) {
            tickOnce(items, world, {}, QcItemEntityManager::TickCallbacks{firstLoaded, keep, noId});
        }
        for (const QcItemEntity &entry : items.items()) {
            if (entry.motion.position.x < 900.0) {
                QVERIFY(!entry.surplus);
            }
        }
        QCOMPARE(items.activeCount(), 300);

        // Toggling the loaded area back and forth keeps every item and its id.
        for (int i = 0; i < 10; ++i) {
            run(items, world, 1, i % 2 == 0);
        }
        QCOMPARE(ids(), before);
        QCOMPARE(items.discardedCount(), 0);
    }

    void freshDropsStayAwake()
    {
        const QcTestWorld world = floorWorld();
        QcItemEntityManager items(m_content, 1);
        QList<QcItemEntityManager::Drop> drops;
        for (int i = 0; i < QcItemEntityManager::activeLimit + 20; ++i) {
            QcItemEntityManager::Drop drop = dropAt({item("qtcraft:dirt"), 1, 0}, {double(i) * 3.0, 64.0, 0.5});
            drop.age = 100.0 + i % 100;
            drops.append(drop);
        }

        quint32 nextId = 1;
        items.restore(drops, [&nextId] { return nextId++; });

        // Mined right after the world opened, before the saved items were counted as awake.
        const quint32 fresh = items.spawn(dropAt({item("qtcraft:stone"), 1, 0}, {-3.5, 64.0, 0.5}), 9999);
        QCOMPARE(fresh, 9999u);
        run(items, world, 1);

        const auto it = std::ranges::find(items.items(), fresh, &QcItemEntity::id);
        QVERIFY(it != items.items().end());
        QVERIFY(!it->surplus);
        QCOMPARE(items.activeCount(), QcItemEntityManager::activeLimit);
    }

    void savedDropsBeyondTheTotalLimitWait()
    {
        const QcTestWorld world = floorWorld();
        QcItemEntityManager full(m_content, 1);
        QList<QcItemEntityManager::Drop> drops;
        for (int i = 0; i < QcItemEntityManager::totalLimit + 7; ++i) {
            drops.append(dropAt({item("qtcraft:dirt"), 1, 0}, {double(i) * 3.0, 64.0, 0.5}));
        }
        quint32 nextId = 1;
        full.restore(drops, [&nextId] { return nextId++; });
        QCOMPARE(full.items().size(), QcItemEntityManager::totalLimit);
        QCOMPARE(full.queued().size(), 7);
        // Waiting drops only appear where a player is near.
        run(full, world, 3, false);
        QCOMPARE(full.queued().size(), 7);
        QCOMPARE(full.discardedCount(), 0);
    }

    void stacksAboveTheLimitAreSplit()
    {
        const QcTestWorld world = floorWorld();
        QcItemEntityManager items(m_content, 1);
        const QcItemId pickaxe = item("qtcraft:stone_pickaxe");
        QVERIFY(items.spawn(dropAt({pickaxe, 5, 0}, {0.5, 64.0, 0.5}), 1) == 1);
        QCOMPARE(items.items().size(), 1);
        QCOMPARE(items.items().first().stack.count, 1);
        run(items, world, 1);
        QCOMPARE(items.items().size(), 5);
        for (const QcItemEntity &entry : items.items()) {
            QCOMPARE(entry.stack, (QcItemStack{pickaxe, 1, 0}));
        }
    }

    void scatteredDropsGetDistinctMotion()
    {
        QcItemEntityManager items(m_content, 1);
        items.spawnScattered({item("qtcraft:dirt"), 1, 0}, {0.5, 64.5, 0.5}, 1);
        items.spawnScattered({item("qtcraft:dirt"), 1, 0}, {0.5, 64.5, 0.5}, 2);
        QCOMPARE(items.items().size(), 2);
        QVERIFY(items.items()[0].motion.velocity != items.items()[1].motion.velocity);
        QVERIFY(items.items()[0].motion.velocity.y > 0.0);
        QCOMPARE(items.items()[0].pickupDelay, QcItemEntityManager::pickupDelay);
        // Empty stacks and unknown items are ignored.
        QCOMPARE(items.spawnScattered({}, {0.5, 64.5, 0.5}, 3), 0u);
        QCOMPARE(items.spawnScattered({9999, 1, 0}, {0.5, 64.5, 0.5}, 4), 0u);
        QCOMPARE(items.items().size(), 2);
    }

    void saplingsNeedTheirGround()
    {
        const QcPlantManager plants(m_content, 1);
        const QcBlockId oak = block("qtcraft:oak_sapling");
        QVERIFY(plants.canPlant(oak, block("qtcraft:grass_block")));
        QVERIFY(plants.canPlant(oak, block("qtcraft:dirt")));
        QVERIFY(!plants.canPlant(oak, block("qtcraft:stone")));
        QVERIFY(!plants.canPlant(oak, qcAirBlockId));
        QVERIFY(!plants.canPlant(oak, std::nullopt));
        QVERIFY(plants.canPlant(block("qtcraft:spruce_sapling"), block("qtcraft:snow_block")));
        // Blocks that are not saplings are never planted.
        QVERIFY(!plants.canPlant(block("qtcraft:dirt"), block("qtcraft:grass_block")));
        QVERIFY(plants.isTrunk(block("qtcraft:oak_log")));
        QVERIFY(plants.isLeaves(block("qtcraft:spruce_leaves")));
        QVERIFY(!plants.isLeaves(block("qtcraft:oak_log")));
    }

    void leavesDecayAfterTheTrunkIsGone()
    {
        QcTestWorld world(*m_content);
        QcTreeShape::Params oak;
        oak.trunk = block("qtcraft:oak_log");
        oak.leaves = block("qtcraft:oak_leaves");
        oak.minHeight = oak.maxHeight = 5;

        const auto put = [&world](QcBlockPos pos, QcBlockId id, bool onlyIntoAir) {
            if (!onlyIntoAir || world.blockAt(pos) == qcAirBlockId) {
                world.set(pos, id);
            }
        };

        QcTreeShape::build(oak, {0, 64, 0}, 0.0, 1, put);
        // A second tree whose crown is more than four blocks away from the first trunk.
        QcTreeShape::build(oak, {20, 64, 0}, 0.0, 1, put);

        const auto leafCount = [&world, this](int fromX, int toX) {
            int count = 0;
            for (int x = fromX; x <= toX; ++x) {
                for (int y = 60; y < 75; ++y) {
                    for (int z = -5; z <= 5; ++z) {
                        count += world.blockAt({x, y, z}) == block("qtcraft:oak_leaves") ? 1 : 0;
                    }
                }
            }
            return count;
        };

        const int crown = leafCount(-5, 5);
        QVERIFY(crown > 20);

        QcPlantManager plants(m_content, 1);
        QcBlockEntities entities(m_content);
        QList<QcBlockPos> broken;
        const auto breakBlock = [&world, &broken](QcBlockPos pos) {
            broken.append(pos);
            world.set(pos, qcAirBlockId);
        };
        const auto setBlock = [&world](QcBlockPos pos, QcBlockId id) {
            world.set(pos, id);
            return true;
        };
        const auto loaded = [](QcBlockPos) { return true; };

        // Removing leaves alone changes nothing.
        world.set({1, 67, 0}, qcAirBlockId);
        plants.blockRemoved({1, 67, 0}, block("qtcraft:oak_leaves"), world);
        QCOMPARE(plants.pendingCheckCount(), 0);

        // One log of five removed: the rest of the trunk still holds the crown.
        world.set({0, 64, 0}, qcAirBlockId);
        plants.blockRemoved({0, 64, 0}, block("qtcraft:oak_log"), world);
        QVERIFY(plants.pendingCheckCount() > 0);
        for (int i = 0; i < ticksFor(QcPlantManager::maxDecayDelay) + 2; ++i) {
            plants.tick(world, entities, dt, {loaded, setBlock, breakBlock});
        }
        QVERIFY(broken.isEmpty());

        // The whole trunk gone: the crown falls apart over a few seconds, never all at once.
        for (int y = 65; y < 69; ++y) {
            world.set({0, y, 0}, qcAirBlockId);
            plants.blockRemoved({0, y, 0}, block("qtcraft:oak_log"), world);
        }

        plants.tick(world, entities, dt, {loaded, setBlock, breakBlock});
        QVERIFY(broken.size() <= QcPlantManager::maxDecaysPerTick);
        for (int i = 0; i < ticksFor(30.0); ++i) {
            plants.tick(world, entities, dt, {loaded, setBlock, breakBlock});
        }

        QCOMPARE(leafCount(-5, 5), 0);
        QCOMPARE(broken.size(), crown - 1);
        QVERIFY(leafCount(15, 25) > 20);
        QCOMPARE(plants.pendingCheckCount(), 0);
    }

    void leavesNextToUnloadedChunksWaitForThem()
    {
        QcTestWorld world(*m_content);
        QcTreeShape::Params oak;
        oak.trunk = block("qtcraft:oak_log");
        oak.leaves = block("qtcraft:oak_leaves");
        oak.minHeight = oak.maxHeight = 5;

        QcTreeShape::build(oak, {0, 64, 0}, 0.0, 1, [&world](QcBlockPos pos, QcBlockId id, bool) { world.set(pos, id); });

        // Everything beyond x/z = 3 counts as unloaded: the outer leaves cannot be judged yet.
        world.loadedRadius = 3;

        QcPlantManager plants(m_content, 1);
        QcBlockEntities entities(m_content);
        for (int y = 64; y < 69; ++y) {
            world.set({0, y, 0}, qcAirBlockId);
            plants.blockRemoved({0, y, 0}, block("qtcraft:oak_log"), world);
        }

        const auto setBlock = [&world](QcBlockPos pos, QcBlockId id) {
            world.set(pos, id);
            return true;
        };
        const auto breakBlock = [&world](QcBlockPos pos) { world.set(pos, qcAirBlockId); };

        for (int i = 0; i < ticksFor(QcPlantManager::maxDecayDelay) + 2; ++i) {
            plants.tick(world, entities, dt, {[](QcBlockPos) { return true; }, setBlock, breakBlock});
        }
        QVERIFY(plants.pendingCheckCount() > 0);
        QCOMPARE(world.blockAt({2, 66, 0}), std::optional<QcBlockId>(block("qtcraft:oak_leaves")));

        // Once loaded, the checks run again and the crown decays.
        world.loadedRadius = 1000;
        for (int i = 0; i < ticksFor(QcPlantManager::recheckSeconds + 10.0); ++i) {
            plants.tick(world, entities, dt, {[](QcBlockPos) { return true; }, setBlock, breakBlock});
        }
        QCOMPARE(plants.pendingCheckCount(), 0);
        QCOMPARE(world.blockAt({2, 66, 0}), std::optional<QcBlockId>(qcAirBlockId));
    }

    void saplingsGrowIntoTreesWhenThereIsRoom()
    {
        QcTestWorld world(*m_content);
        world.fillBox({-6, 63, -6}, {6, 63, 6}, block("qtcraft:grass_block"));
        const QcBlockId sapling = block("qtcraft:oak_sapling");
        world.set({0, 64, 0}, sapling);
        world.set({4, 64, 0}, sapling);

        // The second sapling has a stone roof two blocks up.
        world.set({4, 66, 0}, block("qtcraft:stone"));

        QcPlantManager plants(m_content, 1);
        QcBlockEntities entities(m_content);
        plants.saplingPlaced(entities, {0, 64, 0}, sapling);
        plants.saplingPlaced(entities, {4, 64, 0}, sapling);
        QCOMPARE(entities.saplingsInOrder().size(), 2);

        const auto setBlock = [&world](QcBlockPos pos, QcBlockId id) {
            world.set(pos, id);
            return true;
        };
        const auto breakBlock = [&world](QcBlockPos pos) { world.set(pos, qcAirBlockId); };

        const double maxSeconds = m_content->block(sapling).sapling.maxSeconds;
        // Nothing grows while the chunk is unloaded.
        for (int i = 0; i < ticksFor(maxSeconds + 1.0); ++i) {
            plants.tick(world, entities, dt, {[](QcBlockPos) { return false; }, setBlock, breakBlock});
        }
        QCOMPARE(world.blockAt({0, 64, 0}), std::optional<QcBlockId>(sapling));

        for (int i = 0; i < ticksFor(maxSeconds + 1.0); ++i) {
            plants.tick(world, entities, dt, {[](QcBlockPos) { return true; }, setBlock, breakBlock});
        }
        QCOMPARE(world.blockAt({0, 64, 0}), std::optional<QcBlockId>(block("qtcraft:oak_log")));
        QCOMPARE(world.blockAt({0, 67, 0}), std::optional<QcBlockId>(block("qtcraft:oak_log")));
        QCOMPARE(world.blockAt({1, 68, 0}), std::optional<QcBlockId>(block("qtcraft:oak_leaves")));

        // No room: it stays a sapling and keeps trying.
        QCOMPARE(world.blockAt({4, 64, 0}), std::optional<QcBlockId>(sapling));
        QCOMPARE(entities.saplingsInOrder().size(), 1);
        const QList<QcBlockEntities::Sapling> waiting = entities.saplingsInOrder();
        QVERIFY(waiting.first().remaining <= QcPlantManager::retrySeconds);

        // A broken sapling is forgotten.
        world.set({4, 64, 0}, qcAirBlockId);
        plants.tick(world, entities, dt, {[](QcBlockPos) { return true; }, setBlock, breakBlock});
        QVERIFY(entities.saplingsInOrder().isEmpty());
    }

    void saplingsWithAnUnknownFeatureNeverGrow()
    {
        auto content = std::make_shared<QcContentRegistry>();

        QcBlockDefinition ground;
        ground.name = u"test:ground"_s;
        ground.faceTextures.fill(u"test:ground"_s);

        QcBlockDefinition sapling;
        sapling.name = u"test:sapling"_s;
        sapling.shape.parts = {[] {
            QcShapePart part;
            part.crosses = {{{0.15, 0.0, 0.15}, {0.85, 1.0, 0.85}}};
            return part;
        }()};
        sapling.support = QcBlockSupport::Below;
        sapling.renderLayer = QcRenderLayer::Cutout;
        sapling.solid = false;
        sapling.opaque = false;
        sapling.sapling.feature = u"test:missing"_s;
        sapling.sapling.minSeconds = sapling.sapling.maxSeconds = 1.0;
        sapling.faceTextures.fill(u"test:sapling"_s);

        QVERIFY(content->registerBlock(ground).has_value());
        QVERIFY(content->registerBlock(sapling).has_value());

        QcTestWorld world(*content);
        const QcBlockId saplingId = *content->blockId(u"test:sapling"_s);
        world.set({0, 64, 0}, saplingId);

        QcPlantManager plants(content, 1);
        QcBlockEntities entities(content);
        // Without a plantOn list any solid block will do.
        QVERIFY(plants.canPlant(saplingId, content->blockId(u"test:ground"_s)));
        plants.saplingPlaced(entities, {0, 64, 0}, saplingId);

        for (int i = 0; i < ticksFor(3.0); ++i) {
            plants.tick(
                world, entities, dt, {[](QcBlockPos) { return true; }, [](QcBlockPos, QcBlockId) { return true; }, [](QcBlockPos) {}});
        }

        QCOMPARE(world.blockAt({0, 64, 0}), std::optional<QcBlockId>(saplingId));
        QVERIFY(entities.saplingsInOrder().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestItems)
#include "test_items.moc"
