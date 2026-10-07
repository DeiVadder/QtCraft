#include "QcBaseContent.h"
#include "QcContentPackLoader.h"
#include "QcTestUtil.h"
#include "QcTextureAtlas.h"

#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

} // namespace

class TestContentPack : public QObject
{
    Q_OBJECT

private slots:
    void baseContentLoadsCleanly()
    {
        QStringList warnings;
        const auto content = QcBaseContent::load(warnings);
        QVERIFY2(content.has_value(), qcErrorText(content).constData());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
        // The assignment asks for at least a dozen distinct block types besides air.
        QVERIFY((*content)->blockCount() > 12);
        for (const char *name : {"qtcraft:stone", "qtcraft:grass_block", "qtcraft:water", "qtcraft:oak_leaves",
                                 "qtcraft:glass", "qtcraft:iron_ore", "qtcraft:sand", "qtcraft:snow_block"}) {
            QVERIFY2((*content)->blockId(QString::fromLatin1(name)).has_value(), name);
        }
    }

    void baseBlocksHaveDistinctProperties()
    {
        QStringList warnings;
        const auto content = *QcBaseContent::load(warnings);
        const auto block = [&content](const char *name) { return content->block(*content->blockId(QString::fromLatin1(name))); };
        QVERIFY(block("qtcraft:stone").solid && block("qtcraft:stone").opaque);
        QCOMPARE(block("qtcraft:stone").tool, u"pickaxe"_s);
        QCOMPARE(block("qtcraft:stone").drops.constFirst().item, u"qtcraft:cobblestone"_s);
        QVERIFY(!block("qtcraft:water").solid && block("qtcraft:water").liquid);
        QCOMPARE(block("qtcraft:water").renderLayer, QcRenderLayer::Translucent);
        QCOMPARE(block("qtcraft:oak_leaves").renderLayer, QcRenderLayer::Cutout);
        QVERIFY(!block("qtcraft:glass").opaque);
        QVERIFY(block("qtcraft:bedrock").hardness < 0.0);
        QVERIFY(block("qtcraft:grass_block").texture(QcBlockFace::Up) != block("qtcraft:grass_block").texture(QcBlockFace::North));
    }

    void mobsSurviveTheContentSync()
    {
        QStringList warnings;
        const auto content = *QcBaseContent::load(warnings);
        QVERIFY(content->mobs().size() >= 3);
        QByteArray data;
        {
            QDataStream out(&data, QIODevice::WriteOnly);
            content->writeTo(out);
        }
        QDataStream in(data);
        const auto copy = QcContentRegistry::readFrom(in);
        QVERIFY(copy.has_value());
        QCOMPARE(copy->mobs().size(), content->mobs().size());
        const int zombie = *content->mobIndex(u"qtcraft:zombie"_s);
        QCOMPARE(copy->mobs()[zombie].parts.size(), content->mobs()[zombie].parts.size());
        QCOMPARE(copy->mobs()[zombie].height, content->mobs()[zombie].height);
        for (const QcMobDefinition &mob : content->mobs()) {
            for (const QcMobPart &part : mob.parts) {
                QVERIFY2(content->texture(part.texture), qPrintable(part.texture));
            }
            for (const QString &language : {u"en"_s, u"de"_s}) {
                QVERIFY(content->translate(mob.translationKey, language) != mob.translationKey);
            }
        }
    }

    void everyTextureAndTranslationExists()
    {
        QStringList warnings;
        const auto content = *QcBaseContent::load(warnings);
        const QcTextureAtlas atlas = QcTextureAtlas::build(*content);
        QVERIFY2(atlas.problems().isEmpty(), qPrintable(atlas.problems().join(u'\n')));
        for (const QcBlockDefinition &block : content->blocks()) {
            for (const QString &language : {u"en"_s, u"de"_s}) {
                QVERIFY2(content->translate(block.translationKey, language) != block.translationKey,
                         qPrintable(language + u' ' + block.translationKey));
            }
        }
        QCOMPARE(content->translate(u"block.qtcraft.stone"_s, u"de"_s), u"Stein"_s);
    }

    void brokenEntriesAreSkippedWithWarnings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(u"blocks"_s));
        QVERIFY(QDir(dir.path()).mkpath(u"textures"_s));
        writeFile(dir.filePath(u"pack.json"_s), "{\"id\":\"testpack\",\"version\":\"1.0\"}");
        writeFile(dir.filePath(u"blocks/good.json"_s), "{\"name\":\"good\",\"textures\":{\"all\":\"good\"}}");
        writeFile(dir.filePath(u"blocks/bad.json"_s), "{\"name\":\"bad\",\"solid\":\"maybe\",\"textures\":{\"all\":\"x\"}}");
        writeFile(dir.filePath(u"blocks/broken.json"_s), "{ not json");
        writeFile(dir.filePath(u"textures/good.json"_s), "{\"name\":\"good\",\"pattern\":\"noise\",\"primary\":\"#808080\"}");

        QcContentRegistry registry;
        QStringList warnings;
        const auto info = QcContentPackLoader::load(dir.path(), registry, warnings);
        QVERIFY2(info.has_value(), qcErrorText(info).constData());
        QCOMPARE(info->id, u"testpack"_s);
        QCOMPARE(warnings.size(), 2);
        QVERIFY(registry.blockId(u"testpack:good"_s).has_value());
        QVERIFY(!registry.blockId(u"testpack:bad"_s).has_value());
        QVERIFY(registry.texture(u"testpack:good"_s));
    }

    void manifestErrorsFailThePack_data()
    {
        QTest::addColumn<QByteArray>("manifest");
        QTest::newRow("missing") << QByteArray();
        QTest::newRow("no id") << QByteArray("{\"version\":\"1\"}");
        QTest::newRow("bad id") << QByteArray("{\"id\":\"Bad Id\",\"version\":\"1\"}");
        QTest::newRow("no version") << QByteArray("{\"id\":\"ok\"}");
        QTest::newRow("bad dependencies") << QByteArray("{\"id\":\"ok\",\"version\":\"1\",\"dependencies\":\"base\"}");
    }

    void manifestErrorsFailThePack()
    {
        QFETCH(QByteArray, manifest);
        QTemporaryDir dir;
        if (!manifest.isEmpty()) {
            writeFile(dir.filePath(u"pack.json"_s), manifest);
        }
        QcContentRegistry registry;
        QStringList warnings;
        QVERIFY(!QcContentPackLoader::load(dir.path(), registry, warnings).has_value());
    }

    void atlasFallsBackToPlaceholder()
    {
        QcContentRegistry registry;
        registry.registerTexture({u"a:bad"_s, QJsonObject{{u"pattern"_s, u"nope"_s}}, {}});
        registry.registerTexture({u"a:png"_s, {}, QByteArray("not a png")});
        const QcTextureAtlas atlas = QcTextureAtlas::build(registry);
        QCOMPARE(atlas.problems().size(), 2);
        QCOMPARE(atlas.uv(u"a:bad"_s), atlas.uv(QString::fromLatin1(QcTextureAtlas::missingTextureName)));
        const QImage placeholder = atlas.tile(u"a:unknown"_s);
        QCOMPARE(placeholder.pixelColor(0, 0), QColor(255, 0, 255));
    }

    void atlasDecodesPngTextures()
    {
        QImage source(4, 4, QImage::Format_RGBA8888);
        source.fill(QColor(10, 200, 30));
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(source.save(&buffer, "PNG"));
        QcContentRegistry registry;
        registry.registerTexture({u"a:green"_s, {}, png});
        const QcTextureAtlas atlas = QcTextureAtlas::build(registry);
        QVERIFY(atlas.problems().isEmpty());
        QCOMPARE(atlas.tile(u"a:green"_s).pixelColor(7, 7), QColor(10, 200, 30));
    }
};

QTEST_GUILESS_MAIN(TestContentPack)
#include "test_content_pack.moc"
