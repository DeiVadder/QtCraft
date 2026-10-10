#include "QcBaseContent.h"
#include "QcContentParser.h"
#include "QcPlantGrowth.h"
#include "QcServer.h"
#include "QcServerWorld.h"
#include "QcTestTerrainGenerator.h"

#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

std::shared_ptr<const QcContentRegistry> loadContent()
{
    QStringList warnings;
    return *QcBaseContent::load(warnings);
}

// Free space high above the test terrain.
constexpr int groundY = 100;
constexpr int plantCount = 64;
// Plants stand at z = 8 from x = 16 on, inside loaded chunks on every side (unloaded neighbours
// count as supporting).
constexpr int firstX = 16;
constexpr int plantZ = 8;

} // namespace

// Plants that grow upwards by random ticks (sugar cane): data, rules and pace.
class TestPlantGrowth : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = loadContent();

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
        QVERIFY(world->growingSections().isEmpty());

        plantCane(*world, firstX);
        const QcChunkPos chunk = QcBlockPos{firstX, groundY + 1, plantZ}.chunk();
        QCOMPARE(world->growingSections().chunks().size(), 1);
        QCOMPARE(world->growingSections().sectionsOf(chunk), 1u << ((groundY + 1) / QcWorld::sectionHeight));

        // A cane placed in a higher section marks that section too.
        QVERIFY(world->setBlock({firstX + 2, groundY + 20, plantZ}, id("sugar_cane")));
        QCOMPARE(world->growingSections().sectionsOf(chunk),
                 (1u << ((groundY + 1) / QcWorld::sectionHeight)) | (1u << ((groundY + 20) / QcWorld::sectionHeight)));
        QVERIFY(!world->growingSections().isMarked(id("stone")));
        QVERIFY(world->growingSections().isMarked(id("sugar_cane")));
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
        QStringList warnings;
        const std::shared_ptr<QcContentRegistry> content = *QcBaseContent::load(warnings);
        QcBlockDefinition cane = content->block(id("sugar_cane"));
        cane.growth.averageSeconds = 1.0;
        QVERIFY(content->registerBlock(cane).has_value());

        QcServerConfig config;
        config.viewDistance = 1;
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
