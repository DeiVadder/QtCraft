#include "QcContentParser.h"
#include "QcContentRegistry.h"
#include "QcTestUtil.h"

#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

QcBlockDefinition block(const QString &name)
{
    QcBlockDefinition definition;
    definition.name = name;
    definition.faceTextures.fill(name);
    return definition;
}

} // namespace

class TestContent : public QObject
{
    Q_OBJECT

private slots:
    void airIsAlwaysIdZero()
    {
        const QcContentRegistry registry;
        QCOMPARE(registry.blockCount(), 1);
        QCOMPARE(registry.blockId(u"qtcraft:air"_s), std::optional<QcBlockId>(0));
        QVERIFY(!registry.block(0).solid);
        QVERIFY(registry.block(0).replaceable);
    }

    void idsFollowRegistrationOrderAndOverridesKeepTheirId()
    {
        QcContentRegistry registry;
        QCOMPARE(registry.registerBlock(block(u"a:one"_s)).value(), 1u);
        QCOMPARE(registry.registerBlock(block(u"a:two"_s)).value(), 2u);
        QcBlockDefinition replacement = block(u"a:one"_s);
        replacement.hardness = 7.5;
        QCOMPARE(registry.registerBlock(replacement).value(), 1u);
        QCOMPARE(registry.blockCount(), 3);
        QCOMPARE(registry.block(1).hardness, 7.5);
    }

    void rejectsInvalidNamesAndAirRedefinition()
    {
        QcContentRegistry registry;
        QVERIFY(!registry.registerBlock(block(u"noNamespace"_s)).has_value());
        QVERIFY(!registry.registerBlock(block(u"a:Upper"_s)).has_value());
        QVERIFY(!registry.registerBlock(block(u":empty"_s)).has_value());
        QVERIFY(!registry.registerBlock(block(u"qtcraft:air"_s)).has_value());
        QcBlockDefinition badOpacity = block(u"a:b"_s);
        badOpacity.lightOpacity = 16;
        QVERIFY(!registry.registerBlock(badOpacity).has_value());
        QCOMPARE(registry.blockCount(), 1);
    }

    void translationFallsBackToEnglishThenKey()
    {
        QcContentRegistry registry;
        registry.addTranslations(u"en"_s, {{u"k.stone"_s, u"Stone"_s}, {u"k.dirt"_s, u"Dirt"_s}});
        registry.addTranslations(u"de"_s, {{u"k.stone"_s, u"Stein"_s}});
        QCOMPARE(registry.translate(u"k.stone"_s, u"de"_s), u"Stein"_s);
        QCOMPARE(registry.translate(u"k.dirt"_s, u"de"_s), u"Dirt"_s);
        QCOMPARE(registry.translate(u"k.unknown"_s, u"de"_s), u"k.unknown"_s);
    }

    void serializationRoundTripPreservesEverything()
    {
        QcContentRegistry registry;
        QcBlockDefinition glass = block(u"a:glass"_s);
        glass.renderLayer = QcRenderLayer::Translucent;
        glass.opaque = false;
        glass.lightOpacity = 0;
        glass.hardness = 0.3;
        glass.tool = u"pickaxe"_s;
        glass.toolLevel = 2;
        glass.drops = {{u"a:shard"_s, 3}};
        glass.faceTextures[std::size_t(QcBlockFace::Up)] = u"a:glass_top"_s;
        QVERIFY(registry.registerBlock(glass).has_value());
        registry.registerTexture({u"a:glass"_s, json(R"({"pattern":"glass","seed":3})"), {}});
        registry.registerTexture({u"a:png"_s, {}, QByteArray("\x89PNG-bytes", 10)});
        registry.addTranslations(u"de"_s, {{u"block.a.glass"_s, u"Glas"_s}});

        QByteArray data;
        {
            QDataStream out(&data, QIODevice::WriteOnly);
            registry.writeTo(out);
        }
        QDataStream in(data);
        const QcResult<QcContentRegistry> copy = QcContentRegistry::readFrom(in);
        QVERIFY2(copy.has_value(), qcErrorText(copy).constData());
        QCOMPARE(copy->blockCount(), 2);
        const QcBlockDefinition &read = copy->block(1);
        QCOMPARE(read.name, u"a:glass"_s);
        QCOMPARE(read.renderLayer, QcRenderLayer::Translucent);
        QCOMPARE(read.opaque, false);
        QCOMPARE(read.hardness, 0.3);
        QCOMPARE(read.tool, u"pickaxe"_s);
        QCOMPARE(read.toolLevel, 2);
        QCOMPARE(read.drops.size(), 1);
        QCOMPARE(read.drops.first().item, u"a:shard"_s);
        QCOMPARE(read.drops.first().count, 3);
        QCOMPARE(read.texture(QcBlockFace::Up), u"a:glass_top"_s);
        QCOMPARE(read.texture(QcBlockFace::North), u"a:glass"_s);
        QCOMPARE(copy->textures().size(), 2);
        QCOMPARE(copy->texture(u"a:glass"_s)->procedural.value(u"seed"_s).toInt(), 3);
        QCOMPARE(copy->texture(u"a:png"_s)->png, QByteArray("\x89PNG-bytes", 10));
        QCOMPARE(copy->translate(u"block.a.glass"_s, u"de"_s), u"Glas"_s);
    }

    void truncatedContentIsRejected()
    {
        QcContentRegistry registry;
        QVERIFY(registry.registerBlock(block(u"a:b"_s)).has_value());
        QByteArray data;
        {
            QDataStream out(&data, QIODevice::WriteOnly);
            registry.writeTo(out);
        }
        for (const qsizetype cut : {qsizetype(2), data.size() / 2, data.size() - 1}) {
            QDataStream in(data.left(cut));
            QVERIFY(!QcContentRegistry::readFrom(in).has_value());
        }
    }

    void parserAppliesDefaultsAndNamespaces()
    {
        const auto parsed = QcContentParser::parseBlock(json(R"({"name":"stone","textures":{"all":"stone"}})"), u"base"_s);
        QVERIFY2(parsed.has_value(), qcErrorText(parsed).constData());
        QCOMPARE(parsed->name, u"base:stone"_s);
        QCOMPARE(parsed->translationKey, u"block.base.stone"_s);
        QCOMPARE(parsed->renderLayer, QcRenderLayer::Opaque);
        QVERIFY(parsed->solid);
        QVERIFY(parsed->opaque);
        QCOMPARE(parsed->lightOpacity, 15);
        QCOMPARE(parsed->drops.size(), 1);
        QCOMPARE(parsed->drops.first().item, u"base:stone"_s);
        for (QcBlockFace face : qcAllBlockFaces) {
            QCOMPARE(parsed->texture(face), u"base:stone"_s);
        }
    }

    void parserResolvesFaceTexturePrecedence()
    {
        const auto parsed = QcContentParser::parseBlock(
            json(R"({"name":"grass","textures":{"top":"grass_top","bottom":"other:dirt","side":"grass_side","north":"special"}})"),
            u"base"_s);
        QVERIFY2(parsed.has_value(), qcErrorText(parsed).constData());
        QCOMPARE(parsed->texture(QcBlockFace::Up), u"base:grass_top"_s);
        QCOMPARE(parsed->texture(QcBlockFace::Down), u"other:dirt"_s);
        QCOMPARE(parsed->texture(QcBlockFace::East), u"base:grass_side"_s);
        QCOMPARE(parsed->texture(QcBlockFace::North), u"base:special"_s);
    }

    void parserTranslucentDefaults()
    {
        const auto water = QcContentParser::parseBlock(
            json(R"({"name":"water","render":"translucent","solid":false,"liquid":true,"textures":{"all":"water"}})"), u"b"_s);
        QVERIFY(water.has_value());
        QVERIFY(!water->opaque);
        QCOMPARE(water->lightOpacity, 2);
    }

    void parserRejectsBadInput_data()
    {
        QTest::addColumn<QByteArray>("document");
        QTest::addColumn<QString>("expectedFragment");
        QTest::newRow("missing name") << QByteArray(R"({"textures":{"all":"x"}})") << u"'name' is required"_s;
        QTest::newRow("bad name") << QByteArray(R"({"name":"Bad Name","textures":{"all":"x"}})") << u"not a valid name"_s;
        QTest::newRow("wrong type") << QByteArray(R"({"name":"x","solid":"yes","textures":{"all":"x"}})") << u"'solid'"_s;
        QTest::newRow("range") << QByteArray(R"({"name":"x","lightOpacity":20,"textures":{"all":"x"}})") << u"lightOpacity"_s;
        QTest::newRow("fraction") << QByteArray(R"({"name":"x","toolLevel":1.5,"textures":{"all":"x"}})") << u"integer"_s;
        QTest::newRow("render") << QByteArray(R"({"name":"x","render":"shiny","textures":{"all":"x"}})") << u"'render'"_s;
        QTest::newRow("no textures") << QByteArray(R"({"name":"x"})") << u"'textures'"_s;
        QTest::newRow("missing face") << QByteArray(R"({"name":"x","textures":{"top":"a"}})") << u"every face"_s;
        QTest::newRow("drop count") << QByteArray(R"({"name":"x","drops":[{"item":"y","count":0}],"textures":{"all":"x"}})")
                                    << u"count"_s;
    }

    void parserRejectsBadInput()
    {
        QFETCH(QByteArray, document);
        QFETCH(QString, expectedFragment);
        const auto parsed = QcContentParser::parseBlock(QJsonDocument::fromJson(document).object(), u"b"_s);
        QVERIFY(!parsed.has_value());
        QVERIFY2(parsed.error().contains(expectedFragment), qPrintable(parsed.error()));
    }

    void invisibleBlocksNeedNoTextures()
    {
        const auto parsed = QcContentParser::parseBlock(json(R"({"name":"barrier","render":"invisible"})"), u"b"_s);
        QVERIFY2(parsed.has_value(), qcErrorText(parsed).constData());
    }

    void parsesBiomesWithDefaults()
    {
        const auto biome = QcContentParser::parseBiome(
            json(R"({"name":"dunes","temperature":0.9,"humidity":0.1,"surface":"sand","features":[{"feature":"cactus","density":0.01}]})"),
            u"b"_s);
        QVERIFY2(biome.has_value(), qcErrorText(biome).constData());
        QCOMPARE(biome->name, u"b:dunes"_s);
        QCOMPARE(biome->surfaceBlock, u"b:sand"_s);
        // Omitted layers fall back to the surface block.
        QCOMPARE(biome->subsurfaceBlock, u"b:sand"_s);
        QCOMPARE(biome->underwaterBlock, u"b:sand"_s);
        QCOMPARE(biome->features.size(), 1);
        QCOMPARE(biome->features.first().feature, u"b:cactus"_s);
        QCOMPARE(biome->features.first().density, 0.01);
        QVERIFY(!QcContentParser::parseBiome(json(R"({"name":"x"})"), u"b"_s).has_value());
        QVERIFY(!QcContentParser::parseBiome(json(R"({"name":"x","surface":"s","temperature":2})"), u"b"_s).has_value());
        QVERIFY(!QcContentParser::parseBiome(json(R"({"name":"x","surface":"s","snowLine":100})"), u"b"_s).has_value());
        QVERIFY(!QcContentParser::parseBiome(json(R"({"name":"x","surface":"s","features":[{"density":0.1}]})"), u"b"_s).has_value());
    }

    void parsesFeaturesOresAndTerrain()
    {
        const auto tree = QcContentParser::parseFeature(
            json(R"({"name":"palm","trunk":"log","leaves":"other:leaf","minHeight":5,"maxHeight":8,"ground":["sand"]})"), u"b"_s);
        QVERIFY2(tree.has_value(), qcErrorText(tree).constData());
        QCOMPARE(tree->trunkBlock, u"b:log"_s);
        QCOMPARE(tree->leavesBlock, u"other:leaf"_s);
        QCOMPARE(tree->groundBlocks, (QList<QString>{u"b:sand"_s}));
        QVERIFY(!QcContentParser::parseFeature(json(R"({"name":"t","trunk":"log"})"), u"b"_s).has_value());
        QVERIFY(!QcContentParser::parseFeature(json(R"({"name":"t","trunk":"l","leaves":"x","minHeight":6,"maxHeight":3})"), u"b"_s)
                     .has_value());
        QVERIFY(QcContentParser::parseFeature(json(R"({"name":"c","type":"column","trunk":"cactus"})"), u"b"_s).has_value());

        const auto ore = QcContentParser::parseOre(json(R"({"name":"ruby","block":"ruby_ore","minY":4,"maxY":12})"), u"b"_s);
        QVERIFY2(ore.has_value(), qcErrorText(ore).constData());
        QCOMPARE(ore->block, u"b:ruby_ore"_s);
        QVERIFY(ore->replaces.isEmpty());
        QVERIFY(!QcContentParser::parseOre(json(R"({"name":"r","block":"x","minY":20,"maxY":10})"), u"b"_s).has_value());

        const auto terrain = QcContentParser::parseTerrainSettings(json(R"({"seaLevel":40,"stone":"rock","water":"h2o"})"), u"b"_s);
        QVERIFY2(terrain.has_value(), qcErrorText(terrain).constData());
        QCOMPARE(terrain->seaLevel, 40);
        QCOMPARE(terrain->stoneBlock, u"b:rock"_s);
        QVERIFY(!QcContentParser::parseTerrainSettings(json(R"({"seaLevel":40})"), u"b"_s).has_value());
    }

    void parsesMobs()
    {
        const auto mob = QcContentParser::parseMob(
            json(R"({"name":"boar","health":12,"hostile":true,"spawn":{"time":"night","blocks":["grass"],"caves":true},
                     "model":[{"from":[-0.4,0,-0.6],"to":[0.4,0.8,0.6],"texture":"boar"}],"drops":[{"item":"tusk","count":2}]})"),
            u"b"_s);
        QVERIFY2(mob.has_value(), qcErrorText(mob).constData());
        QCOMPARE(mob->translationKey, u"mob.b.boar"_s);
        QCOMPARE(mob->maxHealth, 12.0);
        QCOMPARE(mob->spawnTime, QcMobDefinition::SpawnTime::Night);
        QCOMPARE(mob->spawnBlocks, (QList<QString>{u"b:grass"_s}));
        QVERIFY(mob->spawnsInCaves);
        QCOMPARE(mob->parts.size(), 1);
        QCOMPARE(mob->parts.first().texture, u"b:boar"_s);
        QCOMPARE(mob->drops.first().item, u"b:tusk"_s);
        // Default chase speed is twice the walking speed.
        QCOMPARE(mob->chaseSpeed, 2.0 * mob->walkSpeed);
        QVERIFY(!QcContentParser::parseMob(json(R"({"name":"x"})"), u"b"_s).has_value());
        QVERIFY(!QcContentParser::parseMob(json(R"({"name":"x","model":[{"from":[0,0,0],"to":[0,1,1],"texture":"t"}]})"), u"b"_s)
                     .has_value());
        QVERIFY(!QcContentParser::parseMob(
                     json(R"({"name":"x","spawn":{"time":"dusk"},"model":[{"from":[0,0,0],"to":[1,1,1],"texture":"t"}]})"), u"b"_s)
                     .has_value());
        QVERIFY(!QcContentParser::parseMob(json(R"({"name":"x","model":[{"from":[0,0,0],"to":[20,1,1],"texture":"t"}]})"), u"b"_s)
                     .has_value());
    }

    void parsesItems()
    {
        const auto pick = QcContentParser::parseItem(
            json(R"({"name":"iron_pickaxe","texture":"pick","tool":"pickaxe","toolLevel":2,"toolSpeed":6,"durability":250})"), u"b"_s);
        QVERIFY2(pick.has_value(), qcErrorText(pick).constData());
        QCOMPARE(pick->translationKey, u"item.b.iron_pickaxe"_s);
        QCOMPARE(pick->texture, u"b:pick"_s);
        // Tools do not stack unless the definition says so.
        QCOMPARE(pick->maxStack, 1);
        QCOMPARE(pick->durability, 250);
        const auto stick = QcContentParser::parseItem(json(R"({"name":"stick"})"), u"b"_s);
        QVERIFY(stick.has_value());
        QCOMPARE(stick->maxStack, 64);
        QCOMPARE(stick->texture, u"b:stick"_s);
        QVERIFY(!QcContentParser::parseItem(json(R"({"name":"x","maxStack":0})"), u"b"_s).has_value());
    }

    void translationsMustBeStrings()
    {
        QVERIFY(QcContentParser::parseTranslations(json(R"({"a":"b"})")).has_value());
        QVERIFY(!QcContentParser::parseTranslations(json(R"({"a":1})")).has_value());
    }
};

QTEST_GUILESS_MAIN(TestContent)
#include "test_content.moc"
