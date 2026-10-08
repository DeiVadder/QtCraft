#include "QcContentParser.h"
#include "QcContentRegistry.h"
#include "QcTestUtil.h"

#include <QJsonArray>
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
    void proceduralTexturesKeepOnlyKnownFields()
    {
        QJsonObject texture = json("{\"name\":\"t\",\"pattern\":\"noise\",\"primary\":\"#112233\",\"seed\":7,\"variation\":0.2}");
        texture.insert(u"padding"_s, QJsonArray::fromVariantList(QVariantList(10000, 0)));
        const QcResult<QcTextureDefinition> parsed = QcContentParser::parseProceduralTexture(texture, u"a"_s);
        QVERIFY2(parsed.has_value(), qcErrorText(parsed));
        QCOMPARE(parsed->procedural.keys(), (QStringList{u"pattern"_s, u"primary"_s, u"seed"_s, u"variation"_s}));
        QVERIFY(!QcContentParser::parseProceduralTexture(json("{\"name\":\"t\",\"pattern\":\"noise\",\"seed\":\"7\"}"), u"a"_s));
        QVERIFY(!QcContentParser::parseProceduralTexture(
            json("{\"name\":\"t\",\"pattern\":\"noise\",\"primary\":\"#1122334455667788990011223344556677\"}"), u"a"_s));
    }

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

    void parsesSaplingsCrossShapesAndDropChances()
    {
        const auto sapling = QcContentParser::parseBlock(
            json(R"({"name":"oak_sapling","shape":"cross","hardness":0,"drops":[{"item":"stick","chance":0.25}],)"
                 R"("sapling":{"feature":"oak_tree","minSeconds":10,"maxSeconds":20,"plantOn":["grass","other:dirt"],"plantChance":0.5},)"
                 R"("textures":{"all":"oak_sapling"}})"),
            u"b"_s);
        QVERIFY2(sapling.has_value(), qcErrorText(sapling).constData());
        QCOMPARE(sapling->shapeName, u"qtcraft:cross"_s);
        QCOMPARE(sapling->support, QcBlockSupport::Below);
        // Cross shapes default to a non-solid cutout block.
        QCOMPARE(sapling->renderLayer, QcRenderLayer::Cutout);
        QVERIFY(!sapling->solid);
        QVERIFY(!sapling->opaque);
        QCOMPARE(sapling->drops.first().chance, 0.25);
        QCOMPARE(sapling->sapling.feature, u"b:oak_tree"_s);
        QCOMPARE(sapling->sapling.minSeconds, 10.0);
        QCOMPARE(sapling->sapling.maxSeconds, 20.0);
        QCOMPARE(sapling->sapling.plantOn, (QList<QString>{u"b:grass"_s, u"other:dirt"_s}));
        QCOMPARE(sapling->sapling.plantChance, 0.5);
        QVERIFY(sapling->sapling.isSapling());

        const auto plain = QcContentParser::parseBlock(json(R"({"name":"stone","textures":{"all":"stone"}})"), u"b"_s);
        QVERIFY(plain->shapeName.isEmpty());
        QVERIFY(plain->shape.isEmpty());
        QCOMPARE(plain->support, QcBlockSupport::None);
        QCOMPARE(plain->drops.first().chance, 1.0);
        QVERIFY(!plain->sapling.isSapling());
    }

    void parserRejectsBadSaplings_data()
    {
        QTest::addColumn<QByteArray>("document");
        QTest::addColumn<QString>("expectedFragment");
        QTest::newRow("shape") << QByteArray(R"({"name":"x","shape":7,"textures":{"all":"x"}})") << u"'shape'"_s;
        QTest::newRow("opaque cross") << QByteArray(R"({"name":"x","shape":"cross","render":"opaque","textures":{"all":"x"}})")
                                      << u"cross-shaped"_s;
        QTest::newRow("chance") << QByteArray(R"({"name":"x","drops":[{"item":"y","chance":1.5}],"textures":{"all":"x"}})")
                                << u"chance"_s;
        QTest::newRow("no feature") << QByteArray(R"({"name":"x","shape":"cross","sapling":{},"textures":{"all":"x"}})")
                                    << u"'feature'"_s;
        QTest::newRow("solid") << QByteArray(R"({"name":"x","sapling":{"feature":"t"},"textures":{"all":"x"}})")
                               << u"must not be solid"_s;
        QTest::newRow("times") << QByteArray(R"({"name":"x","shape":"cross","sapling":{"feature":"t","minSeconds":50,"maxSeconds":10},"textures":{"all":"x"}})")
                               << u"maxSeconds"_s;
        QTest::newRow("plantOn") << QByteArray(R"({"name":"x","shape":"cross","sapling":{"feature":"t","plantOn":[3]},"textures":{"all":"x"}})")
                                 << u"plantOn"_s;
        QTest::newRow("not object") << QByteArray(R"({"name":"x","shape":"cross","sapling":"t","textures":{"all":"x"}})")
                                    << u"'sapling'"_s;
    }

    void parserRejectsBadSaplings()
    {
        QFETCH(QByteArray, document);
        QFETCH(QString, expectedFragment);
        const auto parsed = QcContentParser::parseBlock(QJsonDocument::fromJson(document).object(), u"b"_s);
        QVERIFY(!parsed.has_value());
        QVERIFY2(parsed.error().contains(expectedFragment), qPrintable(parsed.error()));
    }

    void saplingDataSurvivesSerialization()
    {
        QcContentRegistry registry;
        QcBlockDefinition sapling = block(u"a:sapling"_s);
        sapling.shape.parts = {[] {
            QcShapePart part;
            part.crosses = {{{0.1, 0.0, 0.1}, {0.9, 1.0, 0.9}}};
            return part;
        }()};
        sapling.renderLayer = QcRenderLayer::Cutout;
        sapling.solid = false;
        sapling.opaque = false;
        sapling.drops = {{u"a:sapling"_s, 1, 0.3}};
        sapling.sapling = {u"a:tree"_s, 5.0, 9.0, {u"a:dirt"_s}, 0.6};
        QVERIFY(registry.registerBlock(sapling).has_value());
        QByteArray data;
        {
            QDataStream out(&data, QIODevice::WriteOnly);
            registry.writeTo(out);
        }
        QDataStream in(data);
        const QcResult<QcContentRegistry> copy = QcContentRegistry::readFrom(in);
        QVERIFY2(copy.has_value(), qcErrorText(copy).constData());
        const QcBlockDefinition &read = copy->block(1);
        QCOMPARE(read.shape, sapling.shape);
        QCOMPARE(copy->state(1).crosses, (QList<QcAabb>{{{0.1, 0.0, 0.1}, {0.9, 1.0, 0.9}}}));
        QCOMPARE(read.drops.first().chance, 0.3);
        QCOMPARE(read.sapling.feature, u"a:tree"_s);
        QCOMPARE(read.sapling.minSeconds, 5.0);
        QCOMPARE(read.sapling.maxSeconds, 9.0);
        QCOMPARE(read.sapling.plantOn, QList<QString>{u"a:dirt"_s});
        QCOMPARE(read.sapling.plantChance, 0.6);
    }

    void parsesModelsAndAppearances()
    {
        const auto model = QcContentParser::parseModel(
            json(R"({"name":"biped","bones":[{"name":"head","role":"head","pivot":[0,1.4,0],)"
                 R"("boxes":[{"from":[-0.2,1.4,-0.2],"to":[0.2,1.8,0.2],"slot":"head"}]},)"
                 R"({"name":"tail","pivot":[0,1,0.3],"boxes":[{"from":[-0.05,0.9,0.3],"to":[0.05,1,0.8],"slot":"fur"}]}]})"),
            u"m"_s);
        QVERIFY2(model.has_value(), qcErrorText(model).constData());
        QCOMPARE(model->name, u"m:biped"_s);
        QCOMPARE(model->bones.size(), 2);
        QCOMPARE(model->bones.first().role, QcBoneRole::Head);
        QCOMPARE(model->bones.last().role, QcBoneRole::None);
        QCOMPARE(model->bones.last().boxes.first().slot, u"fur"_s);

        const auto outfit = QcContentParser::parseAppearance(
            json(R"({"name":"robe","layers":{"head":{"texture":"skin","tint":"skin"},"fur":{"texture":"fur"}}})"), u"m"_s);
        QVERIFY2(outfit.has_value(), qcErrorText(outfit).constData());
        QCOMPARE(outfit->kind, QcAppearanceDefinition::Kind::Outfit);
        QCOMPARE(outfit->translationKey, u"appearance.m.robe"_s);
        QCOMPARE(outfit->layer(u"head"_s)->tint, QcTint::Skin);
        QCOMPARE(outfit->layer(u"fur"_s)->texture, u"m:fur"_s);
        QCOMPARE(outfit->layer(u"fur"_s)->tint, QcTint::None);
        QVERIFY(!outfit->layer(u"legs"_s));
        QVERIFY(outfit->playable);
        const auto creature = QcContentParser::parseAppearance(
            json(R"({"name":"rags","playable":false,"layers":{"head":{"texture":"skin"}}})"), u"m"_s);
        QVERIFY2(creature.has_value(), qcErrorText(creature).constData());
        QVERIFY(!creature->playable);

        const auto hair = QcContentParser::parseAppearance(
            json(R"({"name":"bun","kind":"hair","layers":{"hair":{"texture":"hair","tint":"hair"}},)"
                 R"("boxes":[{"from":[-0.1,1.8,0],"to":[0.1,1.95,0.2],"slot":"hair"}]})"),
            u"m"_s);
        QVERIFY2(hair.has_value(), qcErrorText(hair).constData());
        QCOMPARE(hair->kind, QcAppearanceDefinition::Kind::Hair);
        QCOMPARE(hair->boxes.size(), 1);

        const auto mob = QcContentParser::parseMob(json(R"({"name":"ghoul","skeleton":"biped","outfit":"robe","armsForward":true})"),
                                                   u"m"_s);
        QVERIFY2(mob.has_value(), qcErrorText(mob).constData());
        QCOMPARE(mob->skeleton, u"m:biped"_s);
        QCOMPARE(mob->outfit, u"m:robe"_s);
        QVERIFY(mob->armsForward);
        QVERIFY(mob->parts.isEmpty());
    }

    void parserRejectsBadModels_data()
    {
        QTest::addColumn<QByteArray>("document");
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("expectedFragment");
        QTest::newRow("no bones") << QByteArray(R"({"name":"x","bones":[]})") << 0 << u"'bones'"_s;
        QTest::newRow("role") << QByteArray(R"({"name":"x","bones":[{"name":"b","role":"wing","pivot":[0,0,0],"boxes":[{"from":[0,0,0],"to":[1,1,1],"slot":"s"}]}]})")
                              << 0 << u"'role'"_s;
        QTest::newRow("pivot") << QByteArray(R"({"name":"x","bones":[{"name":"b","pivot":[0,99,0],"boxes":[{"from":[0,0,0],"to":[1,1,1],"slot":"s"}]}]})")
                               << 0 << u"'pivot'"_s;
        QTest::newRow("empty box") << QByteArray(R"({"name":"x","bones":[{"name":"b","pivot":[0,0,0],"boxes":[{"from":[1,0,0],"to":[1,1,1],"slot":"s"}]}]})")
                                   << 0 << u"'from' < 'to'"_s;
        QTest::newRow("no slot") << QByteArray(R"({"name":"x","bones":[{"name":"b","pivot":[0,0,0],"boxes":[{"from":[0,0,0],"to":[1,1,1]}]}]})")
                                 << 0 << u"'slot'"_s;
        QTest::newRow("kind") << QByteArray(R"({"name":"x","kind":"hat","layers":{"a":{"texture":"t"}}})") << 1 << u"'kind'"_s;
        QTest::newRow("tint") << QByteArray(R"({"name":"x","layers":{"a":{"texture":"t","tint":"green"}}})") << 1 << u"'tint'"_s;
        QTest::newRow("playable") << QByteArray(R"({"name":"x","playable":"no","layers":{"a":{"texture":"t"}}})") << 1
                                  << u"'playable'"_s;
        QTest::newRow("no layers") << QByteArray(R"({"name":"x","layers":{}})") << 1 << u"'layers'"_s;
        QTest::newRow("hair boxes") << QByteArray(R"({"name":"x","kind":"hair","layers":{"a":{"texture":"t"}}})") << 1 << u"'boxes'"_s;
        QTest::newRow("half skeleton") << QByteArray(R"({"name":"x","skeleton":"biped"})") << 2 << u"go together"_s;
        QTest::newRow("no model") << QByteArray(R"({"name":"x"})") << 2 << u"'model'"_s;
    }

    void parserRejectsBadModels()
    {
        QFETCH(QByteArray, document);
        QFETCH(int, kind);
        QFETCH(QString, expectedFragment);
        const QJsonObject object = QJsonDocument::fromJson(document).object();
        const QString error = kind == 0   ? QcContentParser::parseModel(object, u"b"_s).error()
                              : kind == 1 ? QcContentParser::parseAppearance(object, u"b"_s).error()
                                          : QcContentParser::parseMob(object, u"b"_s).error();
        QVERIFY2(error.contains(expectedFragment), qPrintable(error));
    }

    void modelsAndAppearancesSurviveSerialization()
    {
        QcContentRegistry registry;
        QcModelDefinition model;
        model.name = u"a:biped"_s;
        model.bones = {{u"arm"_s, QcBoneRole::RightArm, {0.3, 1.3, 0.0}, {{{0.2, 0.7, -0.1}, {0.4, 1.3, 0.1}, u"arm"_s}}}};
        registry.registerModel(model);
        QcAppearanceDefinition hair;
        hair.name = u"a:bun"_s;
        hair.kind = QcAppearanceDefinition::Kind::Hair;
        hair.layers = {{u"hair"_s, u"a:hair"_s, QcTint::Hair}};
        hair.boxes = {{{-0.1, 1.8, 0.0}, {0.1, 1.9, 0.2}, u"hair"_s}};
        hair.playable = false;
        registry.registerAppearance(hair);
        QcMobDefinition mob;
        mob.name = u"a:ghoul"_s;
        mob.skeleton = u"a:biped"_s;
        mob.outfit = u"a:robe"_s;
        mob.armsForward = true;
        registry.registerMob(mob);
        QByteArray data;
        {
            QDataStream out(&data, QIODevice::WriteOnly);
            registry.writeTo(out);
        }
        QDataStream in(data);
        const QcResult<QcContentRegistry> copy = QcContentRegistry::readFrom(in);
        QVERIFY2(copy.has_value(), qcErrorText(copy).constData());
        QCOMPARE(copy->model(u"a:biped"_s)->bones.first().role, QcBoneRole::RightArm);
        QCOMPARE(copy->model(u"a:biped"_s)->bones.first().boxes.first().to, (QcVec3{0.4, 1.3, 0.1}));
        QCOMPARE(copy->appearance(u"a:bun"_s)->kind, QcAppearanceDefinition::Kind::Hair);
        QCOMPARE(copy->appearance(u"a:bun"_s)->layers.first().tint, QcTint::Hair);
        QCOMPARE(copy->appearance(u"a:bun"_s)->boxes.size(), 1);
        QVERIFY(!copy->appearance(u"a:bun"_s)->playable);
        QCOMPARE(copy->mobs().first().skeleton, u"a:biped"_s);
        QVERIFY(copy->mobs().first().armsForward);
        QVERIFY(!copy->model(u"a:missing"_s));
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

    void parsesMachinesFuelAndFood()
    {
        const auto furnace = QcContentParser::parseBlock(
            json(R"({"name":"kiln","machine":{"category":"firing","speed":2.5},"fuelSeconds":0,"textures":{"all":"k"}})"), u"b"_s);
        QVERIFY2(furnace.has_value(), qcErrorText(furnace).constData());
        QVERIFY(furnace->machine.isMachine());
        QCOMPARE(furnace->machine.category, u"firing"_s);
        QCOMPARE(furnace->machine.speed, 2.5);
        // Without a category a machine smelts; without "machine" a block is none.
        const auto plain = QcContentParser::parseBlock(json(R"({"name":"f","machine":{},"textures":{"all":"k"}})"), u"b"_s);
        QCOMPARE(plain->machine.category, u"smelting"_s);
        QCOMPARE(plain->machine.speed, 1.0);
        const auto log = QcContentParser::parseBlock(json(R"({"name":"log","fuelSeconds":15,"textures":{"all":"l"}})"), u"b"_s);
        QVERIFY(!log->machine.isMachine());
        QCOMPARE(log->fuelSeconds, 15.0);

        const auto coal = QcContentParser::parseItem(json(R"({"name":"coal","fuelSeconds":80})"), u"b"_s);
        QCOMPARE(coal->fuelSeconds, 80.0);
        QCOMPARE(coal->food, 0);
        const auto meat = QcContentParser::parseItem(json(R"({"name":"meat","food":8})"), u"b"_s);
        QCOMPARE(meat->food, 8);
        // Food is counted in whole points up to a full bar.
        QCOMPARE(QcContentParser::parseItem(json(R"({"name":"stew","food":2.6})"), u"b"_s)->food, 3);
        QCOMPARE(QcContentParser::parseItem(json(R"({"name":"feast","food":60})"), u"b"_s)->food, 20);
    }

    void parsesSmeltingRecipes()
    {
        const auto recipe = QcContentParser::parseSmeltingRecipe(
            json(R"({"name":"ingot","input":"ore","result":{"item":"other:ingot","count":2},"seconds":4.5})"), u"b"_s);
        QVERIFY2(recipe.has_value(), qcErrorText(recipe).constData());
        QCOMPARE(recipe->name, u"b:ingot"_s);
        QCOMPARE(recipe->category, u"smelting"_s);
        QCOMPARE(recipe->inputItem, u"b:ore"_s);
        QCOMPARE(recipe->resultItem, u"other:ingot"_s);
        QCOMPARE(recipe->resultCount, 2);
        QCOMPARE(recipe->seconds, 4.5);
        const auto defaults = QcContentParser::parseSmeltingRecipe(
            json(R"({"name":"glass","category":"firing","input":"sand","result":{"item":"glass"}})"), u"b"_s);
        QCOMPARE(defaults->category, u"firing"_s);
        QCOMPARE(defaults->resultCount, 1);
        QCOMPARE(defaults->seconds, 10.0);
    }

    void parserRejectsBadMachineData_data()
    {
        QTest::addColumn<QByteArray>("document");
        QTest::addColumn<bool>("isBlock");
        QTest::newRow("machine not an object") << QByteArray(R"({"name":"x","machine":true,"textures":{"all":"x"}})") << true;
        QTest::newRow("machine speed zero") << QByteArray(R"({"name":"x","machine":{"speed":0},"textures":{"all":"x"}})") << true;
        QTest::newRow("category with namespace") << QByteArray(R"({"name":"x","machine":{"category":"a:b"},"textures":{"all":"x"}})")
                                                 << true;
        QTest::newRow("machine and crafting") << QByteArray(R"({"name":"x","crafting":true,"machine":{},"textures":{"all":"x"}})")
                                              << true;
        QTest::newRow("negative fuel") << QByteArray(R"({"name":"x","fuelSeconds":-1,"textures":{"all":"x"}})") << true;
        QTest::newRow("no input") << QByteArray(R"({"name":"x","result":{"item":"y"}})") << false;
        QTest::newRow("no result") << QByteArray(R"({"name":"x","input":"y"})") << false;
        QTest::newRow("result not an object") << QByteArray(R"({"name":"x","input":"y","result":"z"})") << false;
        QTest::newRow("zero seconds") << QByteArray(R"({"name":"x","input":"y","result":{"item":"z"},"seconds":0})") << false;
        QTest::newRow("count above a stack") << QByteArray(R"({"name":"x","input":"y","result":{"item":"z","count":65}})") << false;
        QTest::newRow("bad category") << QByteArray(R"({"name":"x","category":"Smelting","input":"y","result":{"item":"z"}})")
                                      << false;
    }

    void parserRejectsBadMachineData()
    {
        QFETCH(QByteArray, document);
        QFETCH(bool, isBlock);
        const QJsonObject object = QJsonDocument::fromJson(document).object();
        QVERIFY(!object.isEmpty());
        const bool parsed = isBlock ? QcContentParser::parseBlock(object, u"b"_s).has_value()
                                    : QcContentParser::parseSmeltingRecipe(object, u"b"_s).has_value();
        QVERIFY(!parsed);
    }

    void machinesFuelAndSmeltingSurviveSerialization()
    {
        QcContentRegistry registry;
        QcBlockDefinition furnace = block(u"a:furnace"_s);
        furnace.machine = {u"smelting"_s, 1.5, u"a:crackle"_s};
        QcBlockDefinition log = block(u"a:log"_s);
        log.fuelSeconds = 15.0;
        QVERIFY(registry.registerBlock(furnace).has_value());
        QVERIFY(registry.registerBlock(log).has_value());
        QcItemDefinition meat;
        meat.name = u"a:meat"_s;
        meat.food = 8;
        meat.fuelSeconds = 2.5;
        QVERIFY(registry.registerItem(meat).has_value());
        registry.createBlockItems();
        // Block items inherit the block's fuel time.
        QCOMPARE(registry.item(*registry.itemId(u"a:log"_s)).fuelSeconds, 15.0);
        registry.registerSmeltingRecipe({u"a:cook"_s, u"smelting"_s, u"a:meat"_s, u"a:log"_s, 2, 3.0});

        QByteArray data;
        QDataStream out(&data, QIODevice::WriteOnly);
        registry.writeTo(out);
        QDataStream in(data);
        const QcResult<QcContentRegistry> copy = QcContentRegistry::readFrom(in);
        QVERIFY2(copy.has_value(), qcErrorText(copy).constData());
        const QcBlockDefinition &readFurnace = copy->block(*copy->blockId(u"a:furnace"_s));
        QCOMPARE(readFurnace.machine.category, u"smelting"_s);
        QCOMPARE(readFurnace.machine.speed, 1.5);
        QCOMPARE(copy->block(*copy->blockId(u"a:log"_s)).fuelSeconds, 15.0);
        const QcItemDefinition &readMeat = copy->item(*copy->itemId(u"a:meat"_s));
        QCOMPARE(readMeat.food, 8);
        QCOMPARE(readMeat.fuelSeconds, 2.5);
        QCOMPARE(copy->item(*copy->itemId(u"a:log"_s)).fuelSeconds, 15.0);
        QCOMPARE(copy->smeltingRecipes().size(), 1);
        const QcSmeltingRecipe *cook = copy->smeltingRecipe(u"smelting"_s, u"a:meat"_s);
        QVERIFY(cook);
        QCOMPARE(cook->name, u"a:cook"_s);
        QCOMPARE(cook->resultItem, u"a:log"_s);
        QCOMPARE(cook->resultCount, 2);
        QCOMPARE(cook->seconds, 3.0);
        QVERIFY(!copy->smeltingRecipe(u"firing"_s, u"a:meat"_s));

        // A recipe time outside the parser's range is refused like other corrupt content.
        registry.registerSmeltingRecipe({u"a:cook"_s, u"smelting"_s, u"a:meat"_s, u"a:log"_s, 2, -1.0});
        QByteArray bad;
        QDataStream badOut(&bad, QIODevice::WriteOnly);
        registry.writeTo(badOut);
        QDataStream badIn(bad);
        QVERIFY(!QcContentRegistry::readFrom(badIn).has_value());
    }

    void translationsMustBeStrings()
    {
        QVERIFY(QcContentParser::parseTranslations(json(R"({"a":"b"})")).has_value());
        QVERIFY(!QcContentParser::parseTranslations(json(R"({"a":1})")).has_value());
    }
};

QTEST_GUILESS_MAIN(TestContent)
#include "test_content.moc"
