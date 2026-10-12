#include "QcBaseContent.h"
#include "QcContentParser.h"
#include "QcCropGrowth.h"
#include "QcMobBreeding.h"
#include "QcPlantGrowth.h"
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

// Free space high above the test terrain; soil at groundY, plants one above.
constexpr int groundY = 100;
constexpr int rowLength = 64;
constexpr int firstX = 16;
// The wet row has water along z = wetZ + 1; the dry row lies far from any water.
constexpr int wetZ = 8;
constexpr int dryZ = 24;

} // namespace

// Crops, stems and soil on random ticks (decisions E-289 to E-291): data, pace, light, fruit and
// moisture, without a server.
class TestFarming : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = loadContent();

    QcBlockId id(const char *name) const { return *m_content->blockId(u"qtcraft:"_s + QLatin1StringView(name)); }
    QcBlockId stage(const char *crop, int age) const { return m_content->withProperty(id(crop), u"age"_s, QString::number(age)); }
    QcBlockId soil(bool wet) const { return m_content->withProperty(id("farmland"), u"moist"_s, wet ? u"true"_s : u"false"_s); }
    int ageOf(QcBlockId block) const { return m_content->property(block, u"age"_s).toInt(); }

    std::unique_ptr<QcServerWorld> makeWorld() const
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        for (int cx = 0; cx <= (firstX + rowLength) / QcWorld::chunkSize + 1; ++cx) {
            for (int cz = 0; cz <= dryZ / QcWorld::chunkSize + 1; ++cz) {
                world->loadChunk({cx, cz});
            }
        }
        return world;
    }

    // A row of `crop` at age 0 on farmland along x at `z`, with water beside it when `wet`.
    void plantRow(QcServerWorld &world, const char *crop, int z, bool wet) const
    {
        for (int x = firstX; x < firstX + rowLength; ++x) {
            QVERIFY(world.setBlock({x, groundY, z}, soil(wet)));
            QVERIFY(world.setBlock({x, groundY + 1, z}, stage(crop, 0)));
            if (wet) {
                QVERIFY(world.setBlock({x, groundY, z + 1}, id("water")));
            }
        }
    }

    int stagesOfRow(const QcServerWorld &world, int z) const
    {
        int total = 0;
        for (int x = firstX; x < firstX + rowLength; ++x) {
            total += ageOf(*world.blockAt({x, groundY + 1, z}));
        }
        return total;
    }

    static void grow(QcPlantGrowth &growth, QcServerWorld &world, int seconds)
    {
        for (int i = 0; i < seconds * 20; ++i) {
            growth.tick(world, [&](QcBlockPos pos, QcBlockId block) { return world.setBlock(pos, block); });
        }
    }

    // One random tick of the crop or soil at `pos` with a fixed random generator.
    bool tickAt(const QcPlantGrowth &growth, QcServerWorld &world, QcBlockPos pos) const
    {
        QRandomGenerator random(1);
        return growth.crops().randomTick(world, pos, *world.blockAt(pos), random,
                                         [&](QcBlockPos at, QcBlockId block) { return world.setBlock(at, block); });
    }

    static QcResult<QcBlockDefinition> parseBlock(const char *fields)
    {
        const QString text = uR"({"name":"plant","textures":{"all":"t"},%1})"_s.arg(QLatin1StringView(fields));
        return QcContentParser::parseBlock(QJsonDocument::fromJson(text.toUtf8()).object(), u"m"_s);
    }

private slots:
    void contentDefinesFarming()
    {
        const QcBlockDefinition &farmland = m_content->block(id("farmland"));
        QCOMPARE(farmland.soil.property, u"moist"_s);
        QCOMPARE(farmland.soil.reach, 4);
        QCOMPARE(farmland.soil.revertsTo, u"qtcraft:dirt"_s);
        QCOMPARE(m_content->block(id("dirt")).tillsInto, u"qtcraft:farmland"_s);
        QCOMPARE(m_content->block(id("grass_block")).tillsInto, u"qtcraft:farmland"_s);
        QVERIFY(m_content->block(id("stone")).tillsInto.isEmpty());

        const QcCropDefinition &wheat = m_content->block(id("wheat_crop")).crop;
        QCOMPARE(m_content->block(id("wheat_crop")).property(u"age"_s)->values.size(), 8);
        QCOMPARE(m_content->block(id("carrot_crop")).property(u"age"_s)->values.size(), 4);
        QCOMPARE(wheat.averageSeconds, 1200.0);
        QCOMPARE(wheat.minLight, 9);
        QCOMPARE(wheat.plantChance, 0.9);
        QCOMPARE(m_content->block(id("wheat_crop")).groundBlocks, QStringList{u"qtcraft:farmland"_s});
        QCOMPARE(m_content->block(id("pumpkin_stem")).crop.fruit, u"qtcraft:pumpkin"_s);
        QCOMPARE(m_content->block(id("melon_stem")).crop.fruit, u"qtcraft:melon"_s);

        for (const char *hoe : {"wooden_hoe", "stone_hoe", "iron_hoe", "golden_hoe", "diamond_hoe"}) {
            const QcItemDefinition &item = m_content->item(*m_content->itemId(u"qtcraft:"_s + QLatin1StringView(hoe)));
            QVERIFY(item.tills);
            QCOMPARE(item.tool, u"hoe"_s);
            QVERIFY(item.durability > 0);
        }
        QCOMPARE(m_content->item(*m_content->itemId(u"qtcraft:carrot"_s)).placesBlock, u"qtcraft:carrot_crop"_s);
        QCOMPARE(m_content->mobs()[*m_content->mobIndex(u"qtcraft:pig"_s)].breeding.food,
                 (QStringList{u"qtcraft:carrot"_s, u"qtcraft:potato"_s}));
        QCOMPARE(m_content->mobs()[*m_content->mobIndex(u"qtcraft:sheep"_s)].breeding.food, QStringList{u"qtcraft:wheat"_s});
    }

    void parserChecksFarmingFields()
    {
        const auto crop = parseBlock(R"("shape":"cross","states":{"age":["0","1"]},"crop":{"averageSeconds":60,"plantChance":0.5},
                                        "drops":[{"item":"seed","when":{"age":"1"}}])");
        QVERIFY2(crop.has_value(), qPrintable(crop.error()));
        QCOMPARE(crop->crop.property, u"age"_s);
        QCOMPARE(crop->crop.plantChance, 0.5);
        QCOMPARE(crop->drops.constFirst().when.constFirst().values, QStringList{u"1"_s});
        QVERIFY(crop->hasRandomTicks());

        // Drop conditions name states of the block.
        QVERIFY(!parseBlock(R"("shape":"cross","states":{"age":["0","1"]},"drops":[{"item":"seed","when":{"stage":"1"}}])"));
        QVERIFY(!parseBlock(R"("shape":"cross","states":{"age":["0","1"]},"drops":[{"item":"seed","when":{"age":["1","2"]}}])"));
        QVERIFY(!parseBlock(R"("drops":[{"item":"seed","when":{"age":"1"}}])"));

        // No stage property, a solid crop, a fruit without ground, crops that also grow upwards.
        QVERIFY(!parseBlock(R"("shape":"cross","crop":{})"));
        QVERIFY(!parseBlock(R"("shape":"cross","states":{"age":["0"]},"crop":{})"));
        QVERIFY(!parseBlock(R"("support":"below","states":{"age":["0","1"]},"crop":{}})"));
        QVERIFY(!parseBlock(R"("shape":"cross","states":{"age":["0","1"]},"crop":{"fruit":"melon"})"));
        QVERIFY(!parseBlock(R"("shape":"cross","states":{"age":["0","1"]},"crop":{},"growth":{"maxHeight":3})"));

        const auto soil = parseBlock(R"("states":{"moist":[false,true]},"soil":{"revertsTo":"dirt","reach":2})");
        QVERIFY2(soil.has_value(), qPrintable(soil.error()));
        QCOMPARE(soil->soil.reach, 2);
        QCOMPARE(soil->soil.water, u"qtcraft:water"_s);
        QVERIFY(!parseBlock(R"("states":{"moist":[false,true]},"soil":{"revertsTo":"plant"})"));
        QVERIFY(!parseBlock(R"("states":{"moist":[false,true]},"soil":{})"));
    }

    void stageChanceFollowsTheAverageTime()
    {
        const QcPlantGrowth growth(m_content, 1);
        // Seven steps in 1200 s: one random tick every 68.3 s advances with 68.3 / 171.4.
        const double perTick = QcPlantGrowth::randomTickSeconds / (1200.0 / 7.0);
        QCOMPARE(growth.crops().stageChance(stage("wheat_crop", 0), true), perTick);
        QCOMPARE(growth.crops().stageChance(stage("wheat_crop", 0), false), perTick * 0.5);
        // Three steps for carrots in the same time.
        QCOMPARE(growth.crops().stageChance(stage("carrot_crop", 0), true), QcPlantGrowth::randomTickSeconds / 400.0);
        QCOMPARE(growth.crops().fruitChance(stage("pumpkin_stem", 7), true), QcPlantGrowth::randomTickSeconds / 300.0);
        QCOMPARE(growth.crops().fruitChance(stage("wheat_crop", 7), true), 0.0);
    }

    void cropsGrowTwiceAsFastOnWetFarmland()
    {
        const auto world = makeWorld();
        plantRow(*world, "wheat_crop", wetZ, true);
        plantRow(*world, "wheat_crop", dryZ, false);
        QcPlantGrowth growth(m_content, 5);

        // 600 s: on average 3.5 stages per wet plant (224 for the row) and half that on dry soil;
        // the spread of a row is about 15.
        grow(growth, *world, 600);
        const int wet = stagesOfRow(*world, wetZ);
        const int dry = stagesOfRow(*world, dryZ);
        QVERIFY2(wet >= 170 && wet <= 280, qPrintable(QString::number(wet)));
        QVERIFY2(dry >= 70 && dry <= 160, qPrintable(QString::number(dry)));
        QVERIFY2(wet > dry * 3 / 2, qPrintable(u"%1 %2"_s.arg(wet).arg(dry)));

        // The soil keeps its state: wet next to water, dry under crops without reverting.
        QCOMPARE(*world->blockAt({firstX, groundY, wetZ}), soil(true));
        QCOMPARE(*world->blockAt({firstX, groundY, dryZ}), soil(false));

        // After two hours every plant is ripe and none grows past it.
        grow(growth, *world, 7200);
        QCOMPARE(stagesOfRow(*world, wetZ), 7 * rowLength);
        QCOMPARE(stagesOfRow(*world, dryZ), 7 * rowLength);
    }

    void cropsNeedLight()
    {
        const auto world = makeWorld();
        plantRow(*world, "wheat_crop", wetZ, true);
        // A stone roof two blocks above the plants keeps the sky out.
        for (int x = firstX - 2; x < firstX + rowLength + 2; ++x) {
            for (int z = wetZ - 2; z <= wetZ + 3; ++z) {
                QVERIFY(world->setBlock({x, groundY + 3, z}, id("stone")));
            }
        }
        QCOMPARE(QcCropGrowth::plantLightAt(*world, {firstX, groundY + 1, wetZ}), 0);
        QCOMPARE(QcCropGrowth::plantLightAt(*world, {firstX, groundY + 4, wetZ}), 15);

        QcPlantGrowth growth(m_content, 9);
        grow(growth, *world, 600);
        QCOMPARE(stagesOfRow(*world, wetZ), 0);

        // A lantern beside the first plant lights it enough (15 at the lantern, 14 next to it).
        QVERIFY(world->setBlock({firstX - 1, groundY + 1, wetZ}, id("lantern")));
        QVERIFY(QcCropGrowth::plantLightAt(*world, {firstX, groundY + 1, wetZ}) >= 9);
        grow(growth, *world, 1800);
        QCOMPARE(ageOf(*world->blockAt({firstX, groundY + 1, wetZ})), 7);
        // Far from it nothing grew.
        QCOMPARE(ageOf(*world->blockAt({firstX + 40, groundY + 1, wetZ})), 0);
    }

    void skyLightPassesTransparentBlocks()
    {
        const auto world = makeWorld();
        const QcBlockPos pos{firstX, groundY + 1, wetZ};
        QCOMPARE(QcCropGrowth::plantLightAt(*world, pos), 15);
        QVERIFY(world->setBlock(pos.offset(0, 5, 0), id("glass")));
        QCOMPARE(QcCropGrowth::plantLightAt(*world, pos), 15);
        QVERIFY(world->setBlock(pos.offset(0, 6, 0), id("oak_leaves")));
        QCOMPARE(QcCropGrowth::plantLightAt(*world, pos), 15 - m_content->state(id("oak_leaves")).lightOpacity);
        QVERIFY(world->setBlock(pos.offset(0, 40, 0), id("dirt")));
        QCOMPARE(QcCropGrowth::plantLightAt(*world, pos), 0);
    }

    void ripeStemsPutOneFruitOnSuitableGround()
    {
        const auto world = makeWorld();
        const QcBlockPos stem{firstX + 10, groundY + 1, wetZ};
        QVERIFY(world->setBlock(stem.offset(0, -1, 0), soil(true)));
        QVERIFY(world->setBlock(stem, stage("pumpkin_stem", 7)));
        // Stone below the north and south neighbours, grass below east and west.
        for (const auto &[dx, dz] : {std::pair{0, -1}, std::pair{0, 1}}) {
            QVERIFY(world->setBlock(stem.offset(dx, -1, dz), id("stone")));
        }
        for (const int dx : {-1, 1}) {
            QVERIFY(world->setBlock(stem.offset(dx, -1, 0), id("grass_block")));
        }

        QcPlantGrowth growth(m_content, 3);
        const auto fruitsAround = [&] {
            int count = 0;
            for (const auto &[dx, dz] : {std::pair{0, -1}, std::pair{1, 0}, std::pair{0, 1}, std::pair{-1, 0}}) {
                count += *world->blockAt(stem.offset(dx, 0, dz)) == id("pumpkin") ? 1 : 0;
            }
            return count;
        };
        for (int i = 0; i < 1800 && fruitsAround() == 0; ++i) {
            grow(growth, *world, 1);
        }
        QCOMPARE(fruitsAround(), 1);
        const QcBlockPos fruit = *world->blockAt(stem.offset(1, 0, 0)) == id("pumpkin") ? stem.offset(1, 0, 0) : stem.offset(-1, 0, 0);
        QCOMPARE(*world->blockAt(fruit), id("pumpkin"));

        // While the fruit lies next to it the stem rests (the other grass side stays free); a
        // harvested fruit grows again.
        grow(growth, *world, 7200);
        QCOMPARE(fruitsAround(), 1);
        QCOMPARE(*world->blockAt(stem.offset(0, 0, -1)), qcAirBlockId);
        QCOMPARE(*world->blockAt(stem.offset(0, 0, 1)), qcAirBlockId);
        QVERIFY(world->setBlock(fruit, qcAirBlockId));
        grow(growth, *world, 14400);
        QCOMPARE(fruitsAround(), 1);
    }

    void soilFollowsTheWater()
    {
        const auto world = makeWorld();
        const QcPlantGrowth growth(m_content, 1);
        const QcBlockPos pos{firstX + 20, groundY, dryZ};

        // Dry soil with water four blocks away turns wet; five blocks away is too far (bare dry
        // soil turns back into dirt).
        QVERIFY(world->setBlock(pos, soil(false)));
        QVERIFY(world->setBlock(pos.offset(5, 0, 0), id("water")));
        QVERIFY(tickAt(growth, *world, pos));
        QCOMPARE(*world->blockAt(pos), id("dirt"));
        QVERIFY(world->setBlock(pos, soil(false)));
        QVERIFY(world->setBlock(pos.offset(4, 1, 0), id("water")));
        QVERIFY(tickAt(growth, *world, pos));
        QCOMPARE(*world->blockAt(pos), soil(true));
        QCOMPARE(growth.crops().freshSoil(*world, pos, id("farmland")), soil(true));

        // Without water it dries, then turns back into dirt unless a crop stands on it.
        QVERIFY(world->setBlock(pos.offset(4, 1, 0), qcAirBlockId));
        QCOMPARE(growth.crops().freshSoil(*world, pos, id("farmland")), soil(false));
        QVERIFY(tickAt(growth, *world, pos));
        QCOMPARE(*world->blockAt(pos), soil(false));
        QVERIFY(world->setBlock(pos.offset(0, 1, 0), stage("carrot_crop", 1)));
        QVERIFY(!tickAt(growth, *world, pos));
        QCOMPARE(*world->blockAt(pos), soil(false));
        QVERIFY(world->setBlock(pos.offset(0, 1, 0), qcAirBlockId));
        QVERIFY(tickAt(growth, *world, pos));
        QCOMPARE(*world->blockAt(pos), id("dirt"));

        // A full block on wet soil presses it back into dirt.
        QVERIFY(world->setBlock(pos, soil(true)));
        QVERIFY(world->setBlock(pos.offset(0, 1, 0), id("stone")));
        QVERIFY(tickAt(growth, *world, pos));
        QCOMPARE(*world->blockAt(pos), id("dirt"));
    }

    void soilNextToAnUnloadedChunkWaits()
    {
        const auto world = makeWorld();
        const QcPlantGrowth growth(m_content, 1);
        // x = 1 lies two blocks from chunk -1, which is not loaded.
        const QcBlockPos pos{1, groundY, dryZ};
        QVERIFY(!world->chunk({-1, pos.chunk().z}));
        QVERIFY(world->setBlock(pos, soil(true)));
        QVERIFY(!tickAt(growth, *world, pos));
        QCOMPARE(*world->blockAt(pos), soil(true));
        QCOMPARE(growth.crops().waterNear(*world, pos, m_content->block(id("farmland")).soil), QcCropGrowth::Water::Unknown);

        // Water within the loaded part decides at once; with the neighbour loaded it dries.
        QVERIFY(world->setBlock(pos.offset(1, 0, 0), id("water")));
        QCOMPARE(growth.crops().waterNear(*world, pos, m_content->block(id("farmland")).soil), QcCropGrowth::Water::Near);
        QVERIFY(world->setBlock(pos.offset(1, 0, 0), id("stone")));
        world->loadChunk({-1, pos.chunk().z});
        QVERIFY(tickAt(growth, *world, pos));
        QCOMPARE(*world->blockAt(pos), soil(false));
    }

    void breedingStopsWhenTheAreaOrTheWorldIsFull()
    {
        const QcMobBreeding breeding(m_content);
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        const auto pigAt = [pig](double x, bool dormant = false, bool bred = false) {
            QcMobManager::Mob mob;
            mob.type = pig;
            mob.motion.position = {x, 70.0, 8.0};
            mob.dormant = dormant;
            mob.bred = bred;
            return mob;
        };

        // 23 pigs in the 3 x 3 chunks around x = 8 (dormant ones count), one far away.
        QList<QcMobManager::Mob> mobs;
        for (int i = 0; i < QcMobBreeding::crowdLimit - 1; ++i) {
            mobs.append(pigAt(-15.0 + i, i % 2 == 0));
        }
        mobs.append(pigAt(60.0));
        QVERIFY(breeding.roomFor(mobs, pig, {8.0, 70.0, 8.0}));
        mobs.append(pigAt(30.0));
        QVERIFY(!breeding.roomFor(mobs, pig, {8.0, 70.0, 8.0}));
        // Another kind does not count, nor does the area of another place.
        QVERIFY(breeding.roomFor(mobs, *m_content->mobIndex(u"qtcraft:sheep"_s), {8.0, 70.0, 8.0}));
        QVERIFY(breeding.roomFor(mobs, pig, {200.0, 70.0, 8.0}));

        // The bred animals of a kind are limited in the whole world.
        QList<QcMobManager::Mob> bred;
        for (int i = 0; i < QcMobBreeding::maxBredPerKind - 1; ++i) {
            bred.append(pigAt(1000.0 + 20.0 * i, true, true));
        }
        QVERIFY(breeding.roomFor(bred, pig, {8.0, 70.0, 8.0}));
        bred.append(pigAt(-1000.0, true, true));
        QVERIFY(!breeding.roomFor(bred, pig, {8.0, 70.0, 8.0}));
    }

    void farmlandSectionsGetRandomTicks()
    {
        const auto world = makeWorld();
        QVERIFY(world->growingSections().isEmpty());
        QVERIFY(world->setBlock({firstX, groundY, wetZ}, soil(false)));
        QVERIFY(world->growingSections().isMarked(soil(false)));
        QVERIFY(world->growingSections().isMarked(stage("melon_stem", 3)));
        QVERIFY(!world->growingSections().isMarked(id("wild_carrots")));
        QCOMPARE(world->growingSections().chunks().size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestFarming)
#include "test_farming.moc"
