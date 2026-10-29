// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcTestUtil.h"
#include "worldgen/dungeons/QcDungeonGenerator.h"
#include "worldgen/generators/QcFlatGenerator.h"
#include "worldgen/mineshafts/QcMineshaftGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/towers/QcCoastalTowerGenerator.h"

#include <QSet>
#include <QTest>

#include <array>

using namespace Qt::StringLiterals;

// What /locate finds besides villages and surface structures (decision E-911): the nearest mine
// shaft (with its way in from the surface), dungeon and coastal tower of the terrain generator,
// compared with every region around; the search over regions on its own and its budget of regions;
// the names the generator offers. Fixed
// seeds plus a random one per run (decision E-846).
class TestLocatePlaces : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    const qint64 m_randomSeed = QcTestUtil::randomSeed();

    std::array<qint64, 3> seeds() const { return {42, 1234, m_randomSeed}; }

    // A budget of regions no test search reaches.
    static constexpr qsizetype unlimited = 1'000'000;

    // The nearest place over every region within `rings` of the one holding `from`, by brute force.
    template<typename PlaceOf>
    static std::optional<QcLocatedPlace> bruteForce(QcBlockPos from, int regionSize, int rings, const PlaceOf &placeOf)
    {
        std::optional<QcLocatedPlace> best;
        const int centreX = qcFloorDiv(from.x, regionSize);
        const int centreZ = qcFloorDiv(from.z, regionSize);
        for (int rz = centreZ - rings; rz <= centreZ + rings; ++rz) {
            for (int rx = centreX - rings; rx <= centreX + rings; ++rx) {
                const std::optional<QcLocatedPlace> place = placeOf(rx, rz);
                if (place && (!best || QcPlaceSearch::distance(place->at, from) < QcPlaceSearch::distance(best->at, from))) {
                    best = place;
                }
            }
        }
        return best;
    }

private slots:
    // The search asks regions nearest first and stops beyond the best place: the same answer as
    // asking every region, with few regions asked.
    void theSearchFindsTheNearestOfTheRegions()
    {
        // A place in every third region, at a corner that depends on the region.
        const auto placeOf = [](int regionX, int regionZ) -> std::optional<QcLocatedPlace> {
            if (qcFloorMod(regionX * 7 + regionZ * 13, 3) != 0) {
                return std::nullopt;
            }
            return QcLocatedPlace{{regionX * 100 + qcFloorMod(regionZ * 37, 100), 40, regionZ * 100 + qcFloorMod(regionX * 53, 100)}, std::nullopt};
        };
        for (const QcBlockPos from : {QcBlockPos{0, 64, 0}, QcBlockPos{-517, 64, 233}, QcBlockPos{1999, 64, -1234}}) {
            int asked = 0;
            const auto counting = [&](int regionX, int regionZ) {
                ++asked;
                return placeOf(regionX, regionZ);
            };
            const std::optional<QcLocatedPlace> found = QcPlaceSearch::nearestInRegions(from, 2048, 100, unlimited, counting).place;
            const std::optional<QcLocatedPlace> expected = bruteForce(from, 100, 6, placeOf);
            QVERIFY(found && expected);
            QCOMPARE(QcPlaceSearch::distance(found->at, from), QcPlaceSearch::distance(expected->at, from));
            QVERIFY2(asked <= 25, qPrintable(u"asked %1 regions"_s.arg(asked)));
        }
        // Nothing within reach.
        QVERIFY(!QcPlaceSearch::nearestInRegions(QcBlockPos{}, 500, 100, unlimited, [](int, int) { return std::optional<QcLocatedPlace>(); }).place);
        const auto far = [](int regionX, int regionZ) {
            return regionX == 9 && regionZ == 0 ? std::optional(QcLocatedPlace{{950, 0, 50}, std::nullopt}) : std::nullopt;
        };
        QVERIFY(!QcPlaceSearch::nearestInRegions(QcBlockPos{50, 0, 50}, 500, 100, unlimited, far).place);
        QVERIFY(QcPlaceSearch::nearestInRegions(QcBlockPos{50, 0, 50}, 1000, 100, unlimited, far).place);
    }

    // A miss asks at most the budget of regions (the tick must not plan every region within reach)
    // and says how far it looked: every region nearer than that was asked.
    void aMissStaysWithinTheBudget()
    {
        QSet<std::pair<int, int>> asked;
        const auto nothing = [&asked](int regionX, int regionZ) {
            asked.insert({regionX, regionZ});
            return std::optional<QcLocatedPlace>();
        };
        const QcBlockPos from{-517, 64, 233};
        const QcLocateResult result = QcPlaceSearch::nearestInRegions(from, 2048, 48, 512, nothing);
        QVERIFY(!result.place);
        QCOMPARE(asked.size(), 512);
        QVERIFY2(result.searched > 400 && result.searched < 2048, qPrintable(QString::number(result.searched)));
        for (int rz = qcFloorDiv(from.z - 2048, 48); rz <= qcFloorDiv(from.z + 2048, 48); ++rz) {
            for (int rx = qcFloorDiv(from.x - 2048, 48); rx <= qcFloorDiv(from.x + 2048, 48); ++rx) {
                if (QcPlaceSearch::regionDistance(from, rx, rz, 48) < result.searched) {
                    QVERIFY(asked.contains({rx, rz}));
                }
            }
        }

        // Within the budget the search looks as far as it was asked to.
        asked.clear();
        const QcLocateResult whole = QcPlaceSearch::nearestInRegions(from, 300, 100, 64, nothing);
        QCOMPARE(whole.searched, 300);
        QVERIFY(asked.size() < 64);
    }

    void mineshaftsAreFoundWithTheirWayIn()
    {
        for (const qint64 seed : seeds()) {
            const QcTerrainGenerator terrain(m_content, seed);
            const QcBlockPos from{37, 70, -21};
            const std::optional<QcLocatedPlace> found = terrain.locate(u"mineshaft", from, 2048).place;
            QVERIFY2(found, qPrintable(u"seed %1"_s.arg(seed)));

            const int regionSize = terrain.mineshafts().planner().settings().regionSize;
            const auto placeOf = [&terrain](int regionX, int regionZ) -> std::optional<QcLocatedPlace> {
                const std::shared_ptr<const QcMineshaftPlan> plan = terrain.mineshafts().planOf(regionX, regionZ);
                return plan ? std::optional(QcLocatedPlace{plan->nodes.constFirst().centre, plan->entrance ? std::optional(plan->entrance->out) : std::nullopt})
                            : std::nullopt;
            };
            const int rings = static_cast<int>(QcPlaceSearch::distance(found->at, from)) / regionSize + 1;
            const std::optional<QcLocatedPlace> expected = bruteForce(from, regionSize, rings, placeOf);
            QVERIFY(expected);
            QCOMPARE(QcPlaceSearch::distance(found->at, from), QcPlaceSearch::distance(expected->at, from));
            // The central room lies below the ground, the way in comes out above it.
            if (found->entrance) {
                QVERIFY2(found->entrance->y > found->at.y, qPrintable(u"seed %1"_s.arg(seed)));
            }
        }
    }

    void dungeonsAreFound()
    {
        for (const qint64 seed : seeds()) {
            const QcTerrainGenerator terrain(m_content, seed);
            const QcBlockPos from{-300, 70, 410};
            const std::optional<QcLocatedPlace> found = terrain.locate(u"dungeon", from, 2048).place;
            QVERIFY2(found, qPrintable(u"seed %1"_s.arg(seed)));
            QVERIFY(!found->entrance);

            const int regionSize = terrain.dungeons().planner().settings().regionSize;
            const auto placeOf = [&terrain](int regionX, int regionZ) -> std::optional<QcLocatedPlace> {
                const std::shared_ptr<const QcDungeonPlan> plan = terrain.dungeons().planOf(regionX, regionZ);
                return plan ? std::optional(QcLocatedPlace{plan->origin.offset(plan->sizeX / 2, 1, plan->sizeZ / 2), std::nullopt}) : std::nullopt;
            };
            const int rings = static_cast<int>(QcPlaceSearch::distance(found->at, from)) / regionSize + 1;
            const std::optional<QcLocatedPlace> expected = bruteForce(from, regionSize, rings, placeOf);
            QVERIFY(expected);
            QCOMPARE(QcPlaceSearch::distance(found->at, from), QcPlaceSearch::distance(expected->at, from));
            // The middle of the room's floor is in the room.
            const std::shared_ptr<const QcDungeonPlan> plan =
                terrain.dungeons().planOf(qcFloorDiv(found->at.x, regionSize), qcFloorDiv(found->at.z, regionSize));
            QVERIFY(plan && plan->inRoom(found->at));
        }
    }

    // Coastal towers stand only on coasts: from wherever the search finds one within its budget,
    // no region nearer holds one.
    void coastalTowersAreFound()
    {
        int found = 0;
        for (const qint64 seed : seeds()) {
            const QcTerrainGenerator terrain(m_content, seed);
            const QcBlockPos from{150, 70, -90};
            const QcLocateResult result = terrain.locate(u"coastal_tower", from, 2048);
            if (!result.place || QcPlaceSearch::distance(result.place->at, from) > result.searched) {
                qInfo() << "seed" << seed << "no coastal tower within" << result.searched << "blocks";
                continue;
            }
            ++found;
            QVERIFY(!result.place->entrance);

            const int regionSize = terrain.coastalTowers().planner().settings().regionSize;
            const auto placeOf = [&terrain](int regionX, int regionZ) -> std::optional<QcLocatedPlace> {
                const std::shared_ptr<const QcCoastalTowerPlan> plan = terrain.coastalTowers().planOf(regionX, regionZ);
                return plan ? std::optional(QcLocatedPlace{plan->doorstep, std::nullopt}) : std::nullopt;
            };
            const int rings = static_cast<int>(QcPlaceSearch::distance(result.place->at, from)) / regionSize + 1;
            const std::optional<QcLocatedPlace> expected = bruteForce(from, regionSize, rings, placeOf);
            QVERIFY(expected);
            QCOMPARE(QcPlaceSearch::distance(result.place->at, from), QcPlaceSearch::distance(expected->at, from));
        }
        // The fixed seeds have coasts near the origin.
        QVERIFY(found >= 2);
    }

    void theGeneratorNamesWhatItPlaces()
    {
        const QcTerrainGenerator terrain(m_content, 42);
        const QStringList names = terrain.locatableNames();
        for (const QString &name : {u"village"_s, u"mineshaft"_s, u"dungeon"_s, u"coastal_tower"_s, u"desert_temple"_s, u"witch_hut"_s}) {
            QVERIFY2(names.contains(name), qPrintable(name));
        }
        QVERIFY(!terrain.locate(u"desert_temple", QcBlockPos{}, 2048).place);
        QVERIFY(!terrain.locate(u"nonsense", QcBlockPos{}, 2048).place);

        const QcFlatGenerator flat(m_content, {});
        QVERIFY(flat.locatableNames().isEmpty());
        QVERIFY(!flat.locate(u"mineshaft", QcBlockPos{}, 2048).place);
    }
};

QTEST_GUILESS_MAIN(TestLocatePlaces)
#include "test_locate_places.moc"
