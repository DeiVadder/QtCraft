// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcTestUtil.h"
#include "server/villages/QcVillages.h"
#include "world/QcVillageTestUtil.h"
#include "worldgen/villages/QcVillagePlanCache.h"

#include <QTest>

#include <atomic>
#include <thread>
#include <vector>

using namespace QcVillageTestUtil;

// The village plan cache (decision E-416): it lets go of the least recently used region instead of
// starting over, a plan held elsewhere (QcVillages holds the plans of villages with loaded chunks) is
// never planned again, and the cache stays consistent when several threads use it.
class TestVillagePlanCache : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 42;

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    // A planner that counts its calls; every region gets a village whose id names the region.
    static QcVillagePlanCache::Planner countingPlanner(std::atomic<int> &calls)
    {
        return [&calls](int regionX, int regionZ) {
            calls.fetch_add(1);
            auto plan = std::make_shared<QcVillagePlan>();
            plan->id = static_cast<qint64>(regionX) * 100000 + regionZ;
            return std::shared_ptr<const QcVillagePlan>(std::move(plan));
        };
    }

    // Plans `count` regions far from the villages near the origin, filling the cache.
    static void fill(const QcTerrainGenerator &generator, int count, int row)
    {
        for (int i = 0; i < count; ++i) {
            (void)generator.villages().regionPlan(1000 + i, 1000 + row);
        }
    }

private slots:
    void leastRecentlyUsedRegionsGoFirst()
    {
        std::atomic<int> calls = 0;
        const QcVillagePlanCache cache(4);
        const QcVillagePlanCache::Planner planner = countingPlanner(calls);
        for (int x = 0; x < 4; ++x) {
            QVERIFY(cache.plan(x, 0, planner));
        }
        QCOMPARE(calls.load(), 4);

        // Region 0 used again, then a fifth region: region 1 (least recently used) goes, not all.
        QCOMPARE(cache.plan(0, 0, planner)->id, qint64(0));
        QVERIFY(cache.plan(4, 0, planner));
        QCOMPARE(calls.load(), 5);
        QCOMPARE(cache.size(), 4);
        for (const int x : {0, 2, 3, 4}) {
            QVERIFY(cache.plan(x, 0, planner));
        }
        QCOMPARE(calls.load(), 5);
        QVERIFY(cache.plan(1, 0, planner));
        QCOMPARE(calls.load(), 6);
        QCOMPARE(cache.planned(), quint64(6));
    }

    void heldPlansAreFoundAgain()
    {
        std::atomic<int> calls = 0;
        const QcVillagePlanCache cache(2);
        const QcVillagePlanCache::Planner planner = countingPlanner(calls);
        const std::shared_ptr<const QcVillagePlan> held = cache.plan(7, 7, planner);
        for (int x = 0; x < 10; ++x) {
            (void)cache.plan(x, 0, planner);
        }
        QCOMPARE(calls.load(), 11);

        // Evicted, but still held here: the same plan, not planned again.
        QCOMPARE(cache.plan(7, 7, planner).get(), held.get());
        QCOMPARE(calls.load(), 11);
        QVERIFY(cache.size() <= cache.capacity());
    }

    void threadsShareOneConsistentCache()
    {
        std::atomic<int> calls = 0;
        const QcVillagePlanCache cache(16);
        const QcVillagePlanCache::Planner planner = countingPlanner(calls);
        std::atomic<int> wrong = 0;
        std::vector<std::thread> threads;
        threads.reserve(4);
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&cache, &planner, &wrong, t] {
                for (int i = 0; i < 2000; ++i) {
                    const int x = (i * 7 + t) % 40;
                    if (cache.plan(x, 1, planner)->id != static_cast<qint64>(x) * 100000 + 1) {
                        wrong.fetch_add(1);
                    }
                }
            });
        }
        for (std::thread &thread : threads) {
            thread.join();
        }
        QCOMPARE(wrong.load(), 0);
        QVERIFY(cache.size() <= cache.capacity());
        QCOMPARE(cache.planned(), quint64(calls.load()));
    }

    // A known village with a loaded chunk keeps its plan however many regions are planned after it;
    // its plan is never planned again on the server thread. Once its last chunk is gone it is let go
    // and planned anew, the same.
    void villagesWithLoadedChunksKeepTheirPlans()
    {
        const QcTerrainGenerator generator(m_content, seed);
        QList<std::shared_ptr<const QcVillagePlan>> plans = plansAround(generator, 3);
        QVERIFY(!plans.isEmpty());
        const qint64 id = plans.front()->id;
        const QcBlockPos centre = plans.front()->centre;
        const qsizetype pieces = plans.front()->pieces.size();
        plans.clear();

        QcVillages villages(*m_content, generator);
        villages.noteChunk(*generator.generate(centre.chunk()));
        QVERIFY(villages.find(id));
        QCOMPARE(villages.heldPlans(), 1);
        const QcVillagePlan *held = villages.planOf(id).get();
        QVERIFY(held);

        const QcVillagePlanCache &cache = generator.villages().cache();
        fill(generator, static_cast<int>(cache.capacity()) + 64, 0);
        QVERIFY(cache.size() <= cache.capacity());
        const quint64 planned = cache.planned();
        QCOMPARE(villages.planOf(id).get(), held);
        QCOMPARE(generator.villageFor(centre.chunk()).get(), held);
        villages.noteChunk(*generator.generate({centre.chunk().x + 1, centre.chunk().z}));
        QCOMPARE(cache.planned(), planned);

        // Its chunks leave: the plan is let go, evicted by the next regions, planned again the same.
        villages.forgetChunk(centre.chunk());
        QCOMPARE(villages.heldPlans(), 1);
        villages.forgetChunk({centre.chunk().x + 1, centre.chunk().z});
        QCOMPARE(villages.heldPlans(), 0);
        fill(generator, static_cast<int>(cache.capacity()) + 64, 1);
        const quint64 refilled = cache.planned();
        const std::shared_ptr<const QcVillagePlan> again = villages.planOf(id);
        QCOMPARE(cache.planned(), refilled + 1);
        QVERIFY(again);
        QCOMPARE(again->centre, centre);
        QCOMPARE(again->pieces.size(), pieces);
    }

    // A chunk of a village's reach that entered the world before the village became known (it does
    // not overlap the village's box) holds the plan as well, once the village is known.
    void chunksLoadedBeforeTheVillageHoldItsPlan()
    {
        const QcTerrainGenerator generator(m_content, seed);
        QList<std::shared_ptr<const QcVillagePlan>> plans = plansAround(generator, 3);
        QVERIFY(!plans.isEmpty());
        const qint64 id = plans.front()->id;
        const QcChunkPos inside = plans.front()->centre.chunk();
        const QcChunkPos beside{QcBlockPos{plans.front()->minX, 0, 0}.chunk().x - 1, inside.z};
        QVERIFY(!plans.front()->overlaps(beside, 0));
        plans.clear();

        QSet<QcChunkPos> loaded;
        QcVillages villages(*m_content, generator);
        villages.setLoadedQuery([&loaded](QcChunkPos pos) { return loaded.contains(pos); });
        loaded.insert(beside);
        villages.noteChunk(*generator.generate(beside));
        QVERIFY(!villages.find(id));
        QCOMPARE(villages.heldPlans(), 0);

        loaded.insert(inside);
        villages.noteChunk(*generator.generate(inside));
        QVERIFY(villages.find(id));
        QCOMPARE(villages.heldPlans(), 1);

        // The chunk inside leaves: the one beside still holds the plan.
        loaded.remove(inside);
        villages.forgetChunk(inside);
        QCOMPARE(villages.heldPlans(), 1);
        loaded.remove(beside);
        villages.forgetChunk(beside);
        QCOMPARE(villages.heldPlans(), 0);
    }
};

QTEST_GUILESS_MAIN(TestVillagePlanCache)
#include "test_village_plan_cache.moc"
