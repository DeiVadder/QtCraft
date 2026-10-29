// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcContentParser.h"
#include "fixtures/QcTestUtil.h"
#include "server/chunks/QcSectionMarks.h"
#include "server/core/QcServer.h"
#include "server/core/QcServerWorld.h"
#include "server/plants/QcPlantGrowth.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// Free space high above the test terrain.
constexpr int groundY = 100;
constexpr int plantCount = 64;
// Plants stand at z = 8 from x = 16 on, inside loaded chunks on every side (unloaded neighbours
// count as supporting).
constexpr int firstX = 16;
constexpr int plantZ = 8;

// The test terrain with a sugar cane on sand beside water in the middle of every chunk (local 8, 8
// at groundY), as long as `withCane` is set: chunks that enter the world with growing plants.
class CaneTerrainGenerator : public QcTestTerrainGenerator
{
public:
    explicit CaneTerrainGenerator(const std::shared_ptr<const QcContentRegistry> &content, std::shared_ptr<bool> withCane = {})
        : QcTestTerrainGenerator(content)
        , m_withCane(withCane ? std::move(withCane) : std::make_shared<bool>(true))
        , m_sand(*content->blockId(u"qtcraft:sand"_s))
        , m_water(*content->blockId(u"qtcraft:water"_s))
        , m_cane(*content->blockId(u"qtcraft:sugar_cane"_s))
    {
    }

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override
    {
        std::shared_ptr<QcChunk> chunk = QcTestTerrainGenerator::generate(pos);
        if (*m_withCane) {
            chunk->setBlock(8, groundY, 8, m_sand);
            chunk->setBlock(8, groundY, 9, m_water);
            chunk->setBlock(8, groundY + 1, 8, m_cane);
        }
        return chunk;
    }

private:
    std::shared_ptr<bool> m_withCane;
    QcBlockId m_sand;
    QcBlockId m_water;
    QcBlockId m_cane;
};

} // namespace

// Plants that grow upwards by random ticks (sugar cane): data, rules and pace.
class TestPlantGrowth : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcBlockId id(const char *name) const { return *m_content->blockId(u"qtcraft:"_s + QLatin1StringView(name)); }

    std::unique_ptr<QcServerWorld> makeWorld() const
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        for (int cx = 0; cx <= (firstX + 2 * plantCount) / QcWorld::chunkSize + 1; ++cx) {
            world->loadChunk({cx, 0});
        }
        return world;
    }

    // Sugar cane on sand with water beside the sand, every second column along x.
    void plantCane(QcServerWorld &world, int x) const
    {
        QVERIFY(world.setBlock({x, groundY, plantZ}, id("sand")));
        QVERIFY(world.setBlock({x, groundY, plantZ + 1}, id("water")));
        QVERIFY(world.setBlock({x, groundY + 1, plantZ}, id("sugar_cane")));
    }

    int caneHeight(const QcServerWorld &world, int x) const
    {
        int height = 0;
        while (world.blockAt({x, groundY + 1 + height, plantZ}) == id("sugar_cane")) {
            ++height;
        }
        return height;
    }

    // Runs `ticks` server ticks of growth and returns the grown blocks.
    static int grow(QcPlantGrowth &growth, QcServerWorld &world, int ticks)
    {
        int grown = 0;
        for (int i = 0; i < ticks; ++i) {
            growth.tick(world, [&](QcBlockPos pos, QcBlockId block) {
                ++grown;
                return world.setBlock(pos, block);
            });
        }
        return grown;
    }

private slots:
    void sugarCaneGrowsByData()
    {
        const QcGrowthDefinition &growth = m_content->block(id("sugar_cane")).growth;
        QVERIFY(growth.grows());
        QCOMPARE(growth.maxHeight, 3);
        QCOMPARE(growth.averageSeconds, 600.0);
        QVERIFY(!m_content->block(id("tall_grass")).growth.grows());
    }

    void parserChecksGrowth()
    {
        // A plant block with the given extra fields.
        const auto parse = [](const char *fields) {
            const QString text = uR"({"name":"reed","textures":{"all":"t"},%1})"_s.arg(QLatin1StringView(fields));
            return QcContentParser::parseBlock(QJsonDocument::fromJson(text.toUtf8()).object(), u"m"_s);
        };

        const auto parsed = parse(R"("support":"below","growth":{"maxHeight":5,"averageSeconds":30})");
        QVERIFY2(parsed.has_value(), qPrintable(parsed.error()));
        QCOMPARE(parsed->growth.maxHeight, 5);
        QCOMPARE(parsed->growth.averageSeconds, 30.0);

        QVERIFY(!parse(R"("support":"below","growth":{"maxHeight":1})"));
        QVERIFY(!parse(R"("support":"below","growth":{"maxHeight":65})"));
        QVERIFY(!parse(R"("support":"below","growth":{"averageSeconds":0})"));
        QVERIFY(!parse(R"("growth":{"maxHeight":3})"));
        QVERIFY(!parse(R"("support":"below","growth":3)"));
    }

    void onlySectionsWithGrowingPlantsAreMarked()
    {
        const auto world = makeWorld();
        // The test terrain has no sugar cane, so no section is ticked.
        constexpr QcBlockTrait growing = QcBlockTrait::RandomTicks;
        QVERIFY(world->marks().isEmpty(growing));

        plantCane(*world, firstX);
        const QcChunkPos chunk = QcBlockPos{firstX, groundY + 1, plantZ}.chunk();
        QCOMPARE(world->marks().chunkCount(growing), 1);
        QCOMPARE(world->marks().sectionsOf(chunk, growing), 1u << ((groundY + 1) / QcWorld::sectionHeight));

        // A cane placed in a higher section marks that section too.
        QVERIFY(world->setBlock({firstX + 2, groundY + 20, plantZ}, id("sugar_cane")));
        QCOMPARE(world->marks().sectionsOf(chunk, growing),
                 (1u << ((groundY + 1) / QcWorld::sectionHeight)) | (1u << ((groundY + 20) / QcWorld::sectionHeight)));
        QVERIFY(!world->marks().isMarked(id("stone"), growing));
        QVERIFY(world->marks().isMarked(id("sugar_cane"), growing));
        // Other traits of the same world are marked apart: no circuit, no contact damage here.
        QVERIFY(world->marks().isEmpty(QcBlockTrait::Circuit));
        QCOMPARE(world->marks().sectionsOf(chunk, QcBlockTrait::ContactDamage), 0u);
    }

    void markedChunksComeInAFixedOrderThatFollowsChanges()
    {
        // The random cells are drawn chunk by chunk in this order, so it must not depend on the
        // hash; it is sorted again only when chunks come or go.
        const QcBlockId marked = 1;
        const QcBlockId other = 2;
        constexpr QcBlockTrait growing = QcBlockTrait::RandomTicks;
        std::vector<QcSectionMarks::Traits> traits(3, 0);
        traits[marked] = QcSectionMarks::bit(growing);
        traits[other] = QcSectionMarks::bit(QcBlockTrait::Plate);
        QcSectionMarks marks(traits);
        marks.noteBlock({40, 3, -20}, marked);
        marks.noteBlock({-5, 70, 7}, marked);
        marks.noteBlock({-5, 3, -40}, marked);
        marks.noteBlock({0, 0, 0}, 0);
        // A block of another trait leaves this order alone.
        marks.noteBlock({100, 3, 100}, other);
        QCOMPARE(marks.orderedChunks(growing), (QList<QcChunkPos>{{-1, -3}, {-1, 0}, {2, -2}}));
        QCOMPARE(marks.orderedChunks(QcBlockTrait::Plate), (QList<QcChunkPos>{{6, 6}}));

        // Another block in a known chunk, a new chunk, then one forgotten.
        marks.noteBlock({-6, 20, 8}, marked);
        QCOMPARE(marks.orderedChunks(growing), (QList<QcChunkPos>{{-1, -3}, {-1, 0}, {2, -2}}));
        marks.noteBlock({1, 1, 1}, marked);
        QCOMPARE(marks.orderedChunks(growing), (QList<QcChunkPos>{{-1, -3}, {-1, 0}, {0, 0}, {2, -2}}));
        marks.forgetChunk({-1, 0});
        QCOMPARE(marks.orderedChunks(growing), (QList<QcChunkPos>{{-1, -3}, {0, 0}, {2, -2}}));

        // A chunk noted without marked blocks leaves the order.
        marks.noteChunk({0, 0}, QcChunk());
        QCOMPARE(marks.orderedChunks(growing), (QList<QcChunkPos>{{-1, -3}, {2, -2}}));
        QCOMPARE(marks.chunkCount(growing), 2);
        QCOMPARE(marks.chunkCount(QcBlockTrait::Plate), 1);
    }

    void worldsLoadingChunksInAnotherOrderTickTheSameCells()
    {
        // Decision E-331: the random cells are drawn in the sorted order of the marked chunks, not
        // in the order of the hash. Two worlds that load the same 6 x 6 chunks in opposite orders
        // (the second also loads and unloads other chunks first, so its hash is built differently)
        // grow the same canes in the same ticks with the same seed.
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        QcBlockDefinition cane = content->block(id("sugar_cane"));
        cane.growth.averageSeconds = 1.0;
        QVERIFY(content->registerBlock(cane).has_value());

        QcServerWorld first(content, std::make_unique<CaneTerrainGenerator>(content));
        QcServerWorld second(content, std::make_unique<CaneTerrainGenerator>(content));
        constexpr int side = 6;
        for (int i = 0; i < side * side; ++i) {
            first.loadChunk({i % side, i / side});
        }
        for (int i = 0; i < 20; ++i) {
            second.loadChunk({-10 - i, 40 + i});
        }
        for (int i = side * side - 1; i >= 0; --i) {
            second.loadChunk({i % side, i / side});
        }
        for (int i = 0; i < 20; ++i) {
            second.unloadChunk({-10 - i, 40 + i});
        }
        QCOMPARE(second.loadedChunkCount(), side * side);
        QCOMPARE(first.marks().orderedChunks(QcBlockTrait::RandomTicks), second.marks().orderedChunks(QcBlockTrait::RandomTicks));

        // Every grown block with its tick, in the order it grew.
        const auto growAll = [&](QcServerWorld &world) {
            QcPlantGrowth growth(content, 5);
            QList<std::pair<int, QcBlockPos>> grown;
            for (int tick = 0; tick < 3000; ++tick) {
                growth.tick(world, [&](QcBlockPos pos, QcBlockId block) {
                    grown.append({tick, pos});
                    return world.setBlock(pos, block);
                });
            }
            return grown;
        };
        const QList<std::pair<int, QcBlockPos>> grownFirst = growAll(first);
        // About 80 random ticks hit a cane (36 chunks, 3 cells of 4096 per tick); each grows.
        QVERIFY2(grownFirst.size() >= 20, qPrintable(QString::number(grownFirst.size())));
        QCOMPARE(growAll(second), grownFirst);
    }

    void unloadingAndRegeneratingChunksUpdatesTheOrder()
    {
        // The cached order follows the chunks the world loads, unloads and regenerates.
        const auto withCane = std::make_shared<bool>(true);
        QcServerWorld world(m_content, std::make_unique<CaneTerrainGenerator>(m_content, withCane));
        world.loadChunk({2, 0});
        world.loadChunk({0, 1});
        world.loadChunk({0, 0});
        const QcSectionMarks &marks = world.marks();
        QCOMPARE(marks.orderedChunks(QcBlockTrait::RandomTicks), (QList<QcChunkPos>{{0, 0}, {0, 1}, {2, 0}}));

        // A chunk generated without plants is not in the order; regenerated with a cane it is.
        *withCane = false;
        world.loadChunk({1, 0});
        QCOMPARE(marks.orderedChunks(QcBlockTrait::RandomTicks), (QList<QcChunkPos>{{0, 0}, {0, 1}, {2, 0}}));
        *withCane = true;
        QVERIFY(world.regenerateChunk({1, 0}));
        QCOMPARE(marks.orderedChunks(QcBlockTrait::RandomTicks), (QList<QcChunkPos>{{0, 0}, {0, 1}, {1, 0}, {2, 0}}));

        // An unloaded chunk leaves it; one regenerated without plants too.
        world.unloadChunk({0, 1});
        QCOMPARE(marks.orderedChunks(QcBlockTrait::RandomTicks), (QList<QcChunkPos>{{0, 0}, {1, 0}, {2, 0}}));
        *withCane = false;
        QVERIFY(world.regenerateChunk({2, 0}));
        QCOMPARE(marks.orderedChunks(QcBlockTrait::RandomTicks), (QList<QcChunkPos>{{0, 0}, {1, 0}}));
    }

    void onlyTheTopOfARootedShortColumnGrows()
    {
        const auto world = makeWorld();
        const QcPlantGrowth growth(m_content, 1);
        plantCane(*world, firstX);
        const QcBlockPos cane{firstX, groundY + 1, plantZ};
        QCOMPARE(growth.growthTarget(*world, cane), std::optional<QcBlockPos>(cane.offset(0, 1, 0)));

        // Two tall: still grows; three tall: the maximum.
        QVERIFY(world->setBlock(cane.offset(0, 1, 0), id("sugar_cane")));
        QVERIFY(!growth.growthTarget(*world, cane));
        QCOMPARE(growth.growthTarget(*world, cane.offset(0, 1, 0)), std::optional<QcBlockPos>(cane.offset(0, 2, 0)));
        QVERIFY(world->setBlock(cane.offset(0, 2, 0), id("sugar_cane")));
        QVERIFY(!growth.growthTarget(*world, cane.offset(0, 2, 0)));

        // A block above or a base without water next to it stops growing.
        QVERIFY(world->setBlock(cane.offset(0, 2, 0), qcAirBlockId));
        QVERIFY(world->setBlock(cane.offset(0, 2, 0), id("stone")));
        QVERIFY(!growth.growthTarget(*world, cane.offset(0, 1, 0)));
        QVERIFY(world->setBlock(cane.offset(0, 2, 0), qcAirBlockId));
        QVERIFY(world->setBlock({firstX, groundY, plantZ + 1}, id("sand")));
        QVERIFY(!growth.growthTarget(*world, cane.offset(0, 1, 0)));

        // Other blocks never grow.
        QVERIFY(!growth.growthTarget(*world, {firstX, groundY, plantZ}));
    }

    void growthTakesTheAverageTimeAndStopsAtTheMaximum()
    {
        const auto world = makeWorld();
        QcPlantGrowth growth(m_content, 7);
        for (int i = 0; i < plantCount; ++i) {
            plantCane(*world, firstX + 2 * i);
        }

        // One random tick per block every 68 s with a chance of 68/600 to grow: in 300 s about half
        // a block per column, 32 in all (Poisson spread about 6; 30 seeds average 31).
        QCOMPARE(QcPlantGrowth::randomTickSeconds, 4096.0 / 3.0 / 20.0);
        const int grownEarly = grow(growth, *world, 300 * 20);
        QVERIFY2(grownEarly >= 16 && grownEarly <= 48, qPrintable(QString::number(grownEarly)));

        // After two hours every column has reached three blocks and none more.
        grow(growth, *world, 7200 * 20);
        for (int i = 0; i < plantCount; ++i) {
            QCOMPARE(caneHeight(*world, firstX + 2 * i), 3);
        }
    }

    void theServerGrowsPlantsEveryTick()
    {
        // The base content with a sugar cane that grows at every random tick.
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        QcBlockDefinition cane = content->block(id("sugar_cane"));
        cane.growth.averageSeconds = 1.0;
        QVERIFY(content->registerBlock(cane).has_value());

        QcServerConfig config;
        config.viewDistance = 1;
        config.keepChunksLoaded = true;
        QcServer server(config, content, std::make_unique<QcTestTerrainGenerator>(content));
        QVERIFY(server.openWorld().has_value());
        for (int cx = 0; cx <= (firstX + 2 * plantCount) / QcWorld::chunkSize + 1; ++cx) {
            server.world().loadChunk({cx, 0});
        }
        for (int i = 0; i < plantCount; ++i) {
            plantCane(server.world(), firstX + 2 * i);
        }

        // 100 s: about 1.5 random ticks per block, so most columns grew at least once.
        for (int i = 0; i < 2000; ++i) {
            server.tick();
        }
        int grown = 0;
        for (int i = 0; i < plantCount; ++i) {
            grown += caneHeight(server.world(), firstX + 2 * i) - 1;
        }
        QVERIFY2(grown >= plantCount / 2, qPrintable(QString::number(grown)));
    }
};

QTEST_GUILESS_MAIN(TestPlantGrowth)
#include "test_plant_growth.moc"
