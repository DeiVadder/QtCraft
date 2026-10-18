#include "common/content/loader/QcBaseContent.h"
#include "worldgen/dungeons/QcDungeonGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QElapsedTimer>
#include <QSet>
#include <QTest>

#include <map>

using namespace Qt::StringLiterals;

// Dungeons of the terrain generator (decision E-522) and loot tables (decision E-521): plans are a
// pure function of seed and region, chunks come out the same in any order, corridors end in caves
// within their limits, lights stay away from the spawner's area, chests and spawners are furnished,
// and loot follows its weights.
class TestDungeons : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 381;
    static constexpr int regions = 5;

    std::shared_ptr<const QcContentRegistry> m_content = []() {
        QStringList warnings;
        return *QcBaseContent::load(warnings);
    }();

    QList<std::shared_ptr<const QcDungeonPlan>> plans(const QcTerrainGenerator &terrain) const
    {
        QList<std::shared_ptr<const QcDungeonPlan>> found;
        for (int rz = -regions; rz <= regions; ++rz) {
            for (int rx = -regions; rx <= regions; ++rx) {
                if (std::shared_ptr<const QcDungeonPlan> plan = terrain.dungeons().planOf(rx, rz)) {
                    found.append(std::move(plan));
                }
            }
        }
        return found;
    }

    static QList<QcChunkPos> chunksOf(const QcDungeonPlan &plan)
    {
        QList<QcChunkPos> chunks;
        for (int cx = QcBlockPos{plan.minX, 0, 0}.chunk().x; cx <= QcBlockPos{plan.maxX, 0, 0}.chunk().x; ++cx) {
            for (int cz = QcBlockPos{0, 0, plan.minZ}.chunk().z; cz <= QcBlockPos{0, 0, plan.maxZ}.chunk().z; ++cz) {
                chunks.append({cx, cz});
            }
        }
        return chunks;
    }

    static QcBlockId at(const QHash<QcChunkPos, std::shared_ptr<QcChunk>> &chunks, QcBlockPos pos)
    {
        return chunks.value(pos.chunk())->block(pos.localX(), pos.y, pos.localZ());
    }

    QcBlockId id(const QString &name) const { return *m_content->stateId(name); }

private slots:
    void plansAreAPureFunctionOfSeedAndRegion()
    {
        const QcTerrainGenerator first(m_content, seed);
        const QcTerrainGenerator second(m_content, seed);
        const QList<std::shared_ptr<const QcDungeonPlan>> a = plans(first);
        const QList<std::shared_ptr<const QcDungeonPlan>> b = plans(second);
        QVERIFY2(a.size() >= 10, qPrintable(QString::number(a.size())));
        QCOMPARE(a.size(), b.size());
        for (qsizetype i = 0; i < a.size(); ++i) {
            QCOMPARE(a[i]->origin, b[i]->origin);
            QCOMPARE(a[i]->mob, b[i]->mob);
            QCOMPARE(a[i]->lining, b[i]->lining);
            QCOMPARE(a[i]->corridors.size(), b[i]->corridors.size());
            for (qsizetype c = 0; c < a[i]->corridors.size(); ++c) {
                QCOMPARE(a[i]->corridors[c].path, b[i]->corridors[c].path);
            }
        }

        // Every kind of the data turns up, deep below the ground.
        QSet<QString> kinds;
        for (const auto &plan : a) {
            kinds.insert(plan->mob);
            QVERIFY(plan->origin.y >= 14 && plan->origin.y <= 48);
            QVERIFY(plan->origin.y + plan->sizeY + 6 <= first.column(plan->spawner.x, plan->spawner.z).height);
        }
        QCOMPARE(kinds, (QSet<QString>{u"qtcraft:zombie"_s, u"qtcraft:skeleton"_s, u"qtcraft:spider"_s}));
    }

    void chunksAreTheSameInAnyOrder()
    {
        const QcTerrainGenerator first(m_content, seed);
        const QList<std::shared_ptr<const QcDungeonPlan>> all = plans(first);
        int checked = 0;
        for (const auto &plan : all) {
            if (chunksOf(*plan).size() < 2 || checked == 3) {
                continue;
            }
            ++checked;
            // A fresh generator, chunks in the opposite order.
            const QcTerrainGenerator second(m_content, seed);
            QList<QcChunkPos> chunks = chunksOf(*plan);
            QHash<QcChunkPos, std::shared_ptr<QcChunk>> forward;
            for (const QcChunkPos &pos : std::as_const(chunks)) {
                forward.insert(pos, first.generate(pos));
            }
            std::ranges::reverse(chunks);
            for (const QcChunkPos &pos : std::as_const(chunks)) {
                const std::shared_ptr<QcChunk> other = second.generate(pos);
                for (int y = 0; y < QcWorld::worldHeight; y += 1) {
                    for (int i = 0; i < QcWorld::chunkSize * QcWorld::chunkSize; ++i) {
                        QCOMPARE(other->block(i % 16, y, i / 16), forward.value(pos)->block(i % 16, y, i / 16));
                    }
                }
            }

            // The room as planned.
            QCOMPARE(at(forward, plan->spawner), id(u"qtcraft:spawner"_s));
            for (const QcDungeonPlan::Placed &chest : plan->chests) {
                QCOMPARE(at(forward, chest.at), chest.block);
            }
            const QSet<QcBlockId> walls{id(u"qtcraft:cobblestone"_s), id(u"qtcraft:mossy_cobblestone"_s)};
            QVERIFY(walls.contains(at(forward, plan->origin)));
            QVERIFY(walls.contains(at(forward, plan->origin.offset(plan->sizeX - 1, plan->sizeY - 1, plan->sizeZ - 1))));
            QCOMPARE(at(forward, plan->spawner.offset(0, 1, 0)), qcAirBlockId);
        }
        QCOMPARE(checked, 3);
    }

    void corridorsEndInCavesWithinTheirLimits()
    {
        const QcTerrainGenerator terrain(m_content, seed);
        const QcDungeonSettings &settings = m_content->terrainSettings().dungeons;
        int corridors = 0;
        int lights = 0;
        // A light at `lightDistance` (taxicab, light falls by one per cell) leaves the spawn area dark
        // enough for its spawner.
        const QcBlockDefinition &light = m_content->block(*m_content->stateId(settings.light + u"[attach=north]"_s));
        const QcBlockDefinition &spawner = m_content->block(id(u"qtcraft:spawner"_s));
        QVERIFY2(light.lightLevel - settings.lightDistance <= spawner.spawner.maxLight, qPrintable(QString::number(light.lightLevel)));
        const QList<std::shared_ptr<const QcDungeonPlan>> all = plans(terrain);
        for (const auto &plan : all) {
            QVERIFY(plan->corridors.size() <= settings.corridors);
            for (const QcDungeonPlan::Corridor &corridor : plan->corridors) {
                ++corridors;
                QVERIFY(corridor.path.size() - 1 <= settings.maxLength);
                QCOMPARE(corridor.steps.size(), corridor.path.size() - 1);
                const QcBlockPos end = corridor.path.constLast();
                QVERIFY(terrain.isCave(end.x, end.y, end.z) || terrain.isCave(end.x, end.y + 1, end.z));
                // The door lies in the room's wall, every cell after it outside the room.
                QVERIFY(plan->inRoom(corridor.path.constFirst()));
                for (qsizetype i = 1; i < corridor.path.size(); ++i) {
                    QVERIFY(!plan->inRoom(corridor.path[i]));
                }
            }

            // Carved and open in the generated chunks; the spawner is lit only by the corridors' lights.
            QHash<QcChunkPos, std::shared_ptr<QcChunk>> chunks;
            const QList<QcChunkPos> touched = chunksOf(*plan);
            for (const QcChunkPos &pos : touched) {
                chunks.insert(pos, terrain.generate(pos));
            }
            for (const QcDungeonPlan::Corridor &corridor : plan->corridors) {
                for (const QcBlockPos &feet : corridor.path) {
                    QVERIFY(!m_content->block(at(chunks, feet)).solid);
                    QVERIFY(!m_content->block(at(chunks, feet.offset(0, 1, 0))).solid);
                }
            }
            for (const QcDungeonPlan::Placed &placed : plan->lights) {
                ++lights;
                QVERIFY(plan->distanceToArea(placed.at) >= settings.lightDistance);
                QCOMPARE(at(chunks, placed.at), placed.block);
            }
        }
        qInfo().noquote() << u"%1 dungeons in %2 regions, %3 corridors, %4 lights"_s.arg(all.size()).arg((2 * regions + 1) * (2 * regions + 1)).arg(corridors).arg(lights);
        QVERIFY2(corridors >= 5, qPrintable(QString::number(corridors)));
        QVERIFY2(lights >= 3, qPrintable(QString::number(lights)));
    }

    // Liquid beside a corridor is sealed with wall also across a chunk edge: every column next to a
    // corridor cell lies in the plan's bounds, so the neighbouring chunk runs the plan too, and no
    // generated liquid touches a carved cell.
    void liquidIsSealedAcrossChunkEdges()
    {
        const QcTerrainGenerator terrain(m_content, seed);
        int edgeCells = 0;
        const QList<std::shared_ptr<const QcDungeonPlan>> all = plans(terrain);
        for (const auto &plan : all) {
            QSet<QcBlockPos> carved;
            for (const QcDungeonPlan::Corridor &corridor : plan->corridors) {
                for (const QcBlockPos &feet : corridor.path) {
                    carved.insert(feet);
                    carved.insert(feet.offset(0, 1, 0));
                }
            }
            QHash<QcChunkPos, std::shared_ptr<QcChunk>> chunks;
            const QList<QcChunkPos> touched = chunksOf(*plan);
            for (const QcChunkPos &pos : touched) {
                chunks.insert(pos, terrain.generate(pos));
            }
            constexpr std::array<std::array<int, 3>, 5> around{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}}};
            for (const QcBlockPos &cell : std::as_const(carved)) {
                for (const auto &[dx, dy, dz] : around) {
                    const QcBlockPos next = cell.offset(dx, dy, dz);
                    if (carved.contains(next) || plan->inRoom(next)) {
                        continue;
                    }
                    QVERIFY(plan->touches(next.x, next.z, next.x, next.z));
                    QVERIFY(chunks.contains(next.chunk()));
                    QVERIFY2(!m_content->block(at(chunks, next)).liquid, qPrintable(u"%1 %2 %3"_s.arg(next.x).arg(next.y).arg(next.z)));
                    if (next.chunk() != cell.chunk()) {
                        ++edgeCells;
                        QVERIFY(terrain.dungeons().plansTouching(next.chunk()).contains(plan));
                    }
                }
            }
        }
        qInfo().noquote() << u"%1 cells beside a corridor across a chunk edge"_s.arg(edgeCells);
        QVERIFY(edgeCells > 0);
    }

    // Two dungeons whose plans both touch one chunk are both built into it, the same in any order.
    void twoPlansInOneChunk()
    {
        const QcTerrainGenerator terrain(m_content, seed);
        std::optional<QcChunkPos> shared;
        const QList<std::shared_ptr<const QcDungeonPlan>> all = plans(terrain);
        for (const auto &plan : all) {
            const QList<QcChunkPos> chunks = chunksOf(*plan);
            for (const QcChunkPos &pos : chunks) {
                if (!shared && terrain.dungeons().plansTouching(pos).size() >= 2) {
                    shared = pos;
                }
            }
        }
        QVERIFY(shared);

        const QList<std::shared_ptr<const QcDungeonPlan>> both = terrain.dungeons().plansTouching(*shared);
        // A fresh generator that plans them in the opposite order.
        const QcTerrainGenerator other(m_content, seed);
        for (qsizetype i = both.size() - 1; i >= 0; --i) {
            QVERIFY(other.dungeons().planOf(both[i]->regionX, both[i]->regionZ));
        }
        const std::shared_ptr<QcChunk> a = terrain.generate(*shared);
        const std::shared_ptr<QcChunk> b = other.generate(*shared);
        for (int y = 0; y < QcWorld::worldHeight; ++y) {
            for (int i = 0; i < QcWorld::chunkSize * QcWorld::chunkSize; ++i) {
                QCOMPARE(b->block(i % 16, y, i / 16), a->block(i % 16, y, i / 16));
            }
        }
        for (const auto &plan : both) {
            for (const QcDungeonPlan::Corridor &corridor : plan->corridors) {
                for (const QcBlockPos &feet : corridor.path) {
                    if (feet.chunk() == *shared) {
                        QVERIFY(!m_content->block(a->block(feet.localX(), feet.y, feet.localZ())).solid);
                    }
                }
            }
            if (plan->spawner.chunk() == *shared) {
                QCOMPARE(a->block(plan->spawner.localX(), plan->spawner.y, plan->spawner.localZ()), id(u"qtcraft:spawner"_s));
            }
        }
    }

    void furnishingNamesTheKindAndTheLoot()
    {
        const QcTerrainGenerator terrain(m_content, seed);
        const std::shared_ptr<const QcDungeonPlan> plan = plans(terrain).constFirst();
        const QcChunkFurnishing furnishing = terrain.furnishingIn(plan->spawner.chunk());
        QVERIFY(std::ranges::any_of(furnishing.spawners, [&](const QcChunkFurnishing::Spawner &spawner) {
            return spawner.at == plan->spawner && spawner.mob == plan->mob && !spawner.confined;
        }));
        for (const QcDungeonPlan::Placed &chest : plan->chests) {
            const QList<QcChunkFurnishing::Chest> chests = terrain.furnishingIn(chest.at.chunk()).chests;
            QVERIFY(std::ranges::any_of(chests, [&](const QcChunkFurnishing::Chest &placed) {
                return placed.at == chest.at && placed.lootTable == u"qtcraft:dungeon_chest"_s;
            }));
        }
    }

    void lootFollowsItsWeights()
    {
        const QcLootTableDefinition *table = m_content->lootTables().find(u"qtcraft:dungeon_chest"_s);
        QVERIFY(table);
        const auto items = [this](const QString &name) -> std::optional<QcLootTables::ItemInfo> {
            const std::optional<QcItemId> item = m_content->itemId(name);
            return item ? std::optional(QcLootTables::ItemInfo{*item, m_content->item(*item).maxStack}) : std::nullopt;
        };
        // The same seed, the same loot; spread over distinct slots of a chest.
        QCOMPARE(QcLootTables::draw(*table, 7, items), QcLootTables::draw(*table, 7, items));
        const QList<QcItemStack> chest = QcLootTables::scatter(QcLootTables::draw(*table, 7, items), 27, 7);
        QCOMPARE(chest.size(), 27);

        // Over many draws each entry of the first pool turns up about as often as its weight says.
        std::map<QString, int> counts;
        const int draws = 4000;
        for (int i = 0; i < draws; ++i) {
            QcLootTableDefinition first = *table;
            first.pools = {table->pools.constFirst()};
            first.pools[0].minRolls = 1;
            first.pools[0].maxRolls = 1;
            const QList<QcItemStack> drawn = QcLootTables::draw(first, quint64(i) * 7919, items);
            if (!drawn.isEmpty()) {
                counts[m_content->item(drawn.constFirst().item).name] += 1;
            }
        }
        int total = 0;
        for (const QcLootTableDefinition::Entry &entry : table->pools.constFirst().entries) {
            total += entry.weight;
        }
        for (const QcLootTableDefinition::Entry &entry : table->pools.constFirst().entries) {
            const double expected = double(entry.weight) / total * draws;
            const int seen = counts[entry.item];
            QVERIFY2(std::abs(seen - expected) < 4.0 * std::sqrt(expected) + 5.0, qPrintable(u"%1: %2 of %3"_s.arg(entry.item).arg(seen).arg(expected)));
        }
    }

    // Debug-relative cost of the dungeons: chunks per second with and without them (stage 38.1,
    // docs/Performance-Verlauf.md). Printed, not checked.
    void measureGenerationCost()
    {
        auto without = std::make_shared<QcContentRegistry>(*m_content);
        QcTerrainSettings settings = without->terrainSettings();
        settings.dungeons.enabled = false;
        without->setTerrainSettings(settings);
        const auto rate = [](const QcTerrainGenerator &terrain, int offset) {
            QElapsedTimer timer;
            timer.start();
            int chunks = 0;
            for (int cz = 0; cz < 12; ++cz) {
                for (int cx = 0; cx < 12; ++cx) {
                    terrain.generate({cx + offset, cz});
                    ++chunks;
                }
            }
            return chunks * 1000.0 / std::max<qint64>(1, timer.elapsed());
        };
        const QcTerrainGenerator with(m_content, seed);
        const QcTerrainGenerator plain(without, seed);
        // Interleaved, two rounds each on fresh areas.
        const double a1 = rate(plain, 0);
        const double b1 = rate(with, 0);
        const double a2 = rate(plain, 40);
        const double b2 = rate(with, 40);
        qInfo().noquote() << u"chunks/s without dungeons %1, with %2"_s.arg((a1 + a2) / 2, 0, 'f', 1).arg((b1 + b2) / 2, 0, 'f', 1);
    }
};

QTEST_GUILESS_MAIN(TestDungeons)
#include "test_dungeons.moc"
