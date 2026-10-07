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

    void translationsMustBeStrings()
    {
        QVERIFY(QcContentParser::parseTranslations(json(R"({"a":"b"})")).has_value());
        QVERIFY(!QcContentParser::parseTranslations(json(R"({"a":1})")).has_value());
    }
};

QTEST_GUILESS_MAIN(TestContent)
#include "test_content.moc"
