// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcEntityMesher.h"
#include "clientcore/session/QcExplosionEffects.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/loader/QcContentPackLoader.h"
#include "common/gameplay/QcCrafting.h"
#include "QcTestUtil.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
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
    // Every definition folder the loader reads counts for the pack's size and fingerprint
    // (contentFiles): the folders are read from the loader's source, so a new folder cannot be
    // forgotten there.
    void contentFilesCoverEveryDefinitionFolder()
    {
        QFile source(QStringLiteral(QTCRAFT_SOURCE_DIR "/src/common/content/loader/QcContentPackLoader.cpp"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QString code = QString::fromUtf8(source.readAll());
        static const QRegularExpression folderOf(uR"re(loadDefinitions\(u"([a-z]+)"_s)re"_s);
        QStringList folders;
        for (QRegularExpressionMatchIterator it = folderOf.globalMatch(code); it.hasNext();) {
            folders.append(it.next().captured(1));
        }
        QVERIFY(folders.size() >= 20);
        QVERIFY(folders.contains(u"enchantments"_s));

        QTemporaryDir dir;
        for (const QString &folder : std::as_const(folders)) {
            QVERIFY(QDir(dir.path()).mkpath(folder));
            writeFile(QDir(dir.path()).filePath(folder + u"/x.json"_s), "{}");
        }
        const QStringList files = QcContentPackLoader::contentFiles(dir.path());
        for (const QString &folder : std::as_const(folders)) {
            QVERIFY2(files.contains(QDir(dir.path()).filePath(folder + u"/x.json"_s)), qPrintable(folder));
        }
    }

    void baseContentLoadsCleanly()
    {
        const QcResult<QcBaseContent::Loaded> loaded = QcBaseContent::load();
        QVERIFY2(loaded.has_value(), qcErrorText(loaded).constData());
        QVERIFY2(loaded->warnings.isEmpty(), qPrintable(loaded->warnings.join(u'\n')));
        const std::shared_ptr<QcContentRegistry> &content = loaded->registry;

        // The assignment asks for at least a dozen distinct block types besides air.
        QVERIFY(content->blockCount() > 12);
        for (const char *name : {"qtcraft:stone", "qtcraft:grass_block", "qtcraft:water", "qtcraft:oak_leaves",
                                 "qtcraft:glass", "qtcraft:iron_ore", "qtcraft:sand", "qtcraft:snow_block"}) {
            QVERIFY2(content->blockId(QString::fromLatin1(name)).has_value(), name);
        }
    }

    void baseBlocksHaveDistinctProperties()
    {
        const auto content = QcTestUtil::baseContent()->registry;
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

    void smeltingReplacesTheIngotRecipes()
    {
        const auto content = QcTestUtil::baseContent()->registry;

        // Ingots, glass and bricks come from the furnace now, not from ores in the crafting grid (E-037);
        // the grid only packs nuggets or storage blocks back into ingots and bricks into a block
        // (E-200, E-204).
        const QList<std::pair<const char *, QStringList>> onlyFrom{
            {"qtcraft:iron_ingot", {u"qtcraft:iron_nugget"_s, u"qtcraft:iron_block"_s}},
            {"qtcraft:gold_ingot", {u"qtcraft:gold_nugget"_s, u"qtcraft:gold_block"_s}},
            {"qtcraft:glass", {}},
            {"qtcraft:bricks", {u"qtcraft:brick"_s}}};
        for (const auto &[result, sources] : onlyFrom) {
            for (const QcRecipeDefinition &recipe : content->recipes()) {
                if (recipe.resultItem != QLatin1StringView(result)) {
                    continue;
                }
                QStringList used = recipe.ingredients;
                for (const auto &key : recipe.keys) {
                    used.append(key.second);
                }
                QVERIFY2(!used.isEmpty(), qPrintable(recipe.name));
                QVERIFY2(std::ranges::all_of(used, [&sources](const QString &name) { return sources.contains(name); }),
                         qPrintable(recipe.name));
            }
        }

        const QList<std::pair<const char *, const char *>> smelting{
            {"qtcraft:raw_iron", "qtcraft:iron_ingot"}, {"qtcraft:raw_gold", "qtcraft:gold_ingot"},
            {"qtcraft:sand", "qtcraft:glass"},         {"qtcraft:clay_ball", "qtcraft:brick"},
            {"qtcraft:clay", "qtcraft:terracotta"},    {"qtcraft:cobblestone", "qtcraft:stone"},
            {"qtcraft:oak_log", "qtcraft:charcoal"},   {"qtcraft:raw_porkchop", "qtcraft:cooked_porkchop"},
            {"qtcraft:rotten_flesh", "qtcraft:leather_scrap"}};
        for (const auto &[input, result] : smelting) {
            const QcSmeltingRecipe *recipe = content->smeltingRecipe(u"smelting"_s, QString::fromLatin1(input));
            QVERIFY2(recipe && recipe->resultItem == QLatin1StringView(result), input);
        }

        const QcBlockDefinition &furnace = content->block(*content->blockId(u"qtcraft:furnace"_s));
        QCOMPARE(furnace.machine.category, u"smelting"_s);
        QVERIFY(furnace.texture(QcBlockFace::North) != furnace.texture(QcBlockFace::East));
        const QcRecipeDefinition &recipe = content->recipes()[*content->recipeIndex(u"qtcraft:furnace"_s)];
        QCOMPARE(QcCrafting::requirements(recipe), (QList<std::pair<QString, int>>{{u"qtcraft:cobblestone"_s, 8}}));

        const auto fuel = [&content](const char *name) { return content->item(*content->itemId(QString::fromLatin1(name))).fuelSeconds; };
        QCOMPARE(fuel("qtcraft:coal"), 80.0);
        QCOMPARE(fuel("qtcraft:charcoal"), 80.0);
        QCOMPARE(fuel("qtcraft:oak_planks"), 15.0);
        QCOMPARE(fuel("qtcraft:cobblestone"), 0.0);
        QVERIFY(content->item(*content->itemId(u"qtcraft:cooked_porkchop"_s)).food
                > content->item(*content->itemId(u"qtcraft:raw_porkchop"_s)).food);
    }

    void mobsSurviveTheContentSync()
    {
        const auto content = QcTestUtil::baseContent()->registry;
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
                QVERIFY2(content->assets().texture(part.texture), qPrintable(part.texture));
            }
            for (const QString &language : {u"en"_s, u"de"_s}) {
                QVERIFY(content->translations().translate(mob.translationKey, language) != mob.translationKey);
            }
        }
    }

    void everyTextureAndTranslationExists()
    {
        const auto content = QcTestUtil::baseContent()->registry;
        const QcTextureAtlas atlas = QcTextureAtlas::build(*content);
        QVERIFY2(atlas.problems().isEmpty(), qPrintable(atlas.problems().join(u'\n')));

        for (const QcBlockDefinition &block : content->blocks()) {
            for (const QString &language : {u"en"_s, u"de"_s}) {
                QVERIFY2(content->translations().translate(block.translationKey, language) != block.translationKey,
                         qPrintable(language + u' ' + block.translationKey));
            }
        }

        QCOMPARE(content->translations().translate(u"block.qtcraft.stone"_s, u"de"_s), u"Stein"_s);
    }

    // The particles the client draws by a fixed texture name (explosion flash and smoke, the campfire
    // smoke, experience orbs) find their tile in the atlas, not the missing placeholder.
    void particleTexturesAreInTheAtlas()
    {
        const auto content = QcTestUtil::baseContent()->registry;
        const QcTextureAtlas atlas = QcTextureAtlas::build(*content);
        const QRectF missing = atlas.uv(QString::fromLatin1(QcTextureAtlas::missingTextureName));
        for (const char *texture : {QcExplosionEffects::flashTexture, QcExplosionEffects::smokeTexture, QcEntityMesher::orbTexture}) {
            QVERIFY2(atlas.uv(QString::fromLatin1(texture)) != missing, texture);
        }
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
        const auto loaded = QcContentPackLoader::load(dir.path(), registry);
        QVERIFY2(loaded.has_value(), qcErrorText(loaded).constData());
        QCOMPARE(loaded->info.id, u"testpack"_s);
        QCOMPARE(loaded->warnings.size(), 2);
        QVERIFY(registry.blockId(u"testpack:good"_s).has_value());
        QVERIFY(!registry.blockId(u"testpack:bad"_s).has_value());
        QVERIFY(registry.assets().texture(u"testpack:good"_s));
    }

    // weather.json names its snow layer: a block of the pack that is not opaque and needs support
    // below; anything else is refused with a warning and the weather settings stay as they were.
    void weatherRefusesAnUnfitSnowLayer_data()
    {
        QTest::addColumn<QByteArray>("layer");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("powder") << QByteArray("powder") << true;
        QTest::newRow("missing block") << QByteArray("nothing") << false;
        QTest::newRow("opaque block") << QByteArray("stone") << false;
        QTest::newRow("no support") << QByteArray("glass") << false;
    }

    void weatherRefusesAnUnfitSnowLayer()
    {
        QFETCH(QByteArray, layer);
        QFETCH(bool, accepted);
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath(u"blocks"_s));
        writeFile(dir.filePath(u"pack.json"_s), R"({"id":"testpack","version":"1.0"})");
        writeFile(dir.filePath(u"blocks/powder.json"_s),
                  R"({"name":"powder","opaque":false,"support":"below","textures":{"all":"powder"}})");
        writeFile(dir.filePath(u"blocks/stone.json"_s), R"({"name":"stone","support":"below","textures":{"all":"stone"}})");
        writeFile(dir.filePath(u"blocks/glass.json"_s), R"({"name":"glass","opaque":false,"textures":{"all":"glass"}})");
        writeFile(dir.filePath(u"weather.json"_s), R"({"lightning": {"damage": 2}, "snow": {"layer": ")" + layer + R"("}})");

        QcContentRegistry registry;
        const QcResult<QcLoadedPack> loaded = QcContentPackLoader::load(dir.path(), registry);
        QVERIFY(loaded.has_value());
        const QStringList &warnings = loaded->warnings;
        QCOMPARE(warnings.isEmpty(), accepted);
        QCOMPARE(registry.weather().snow.layer, accepted ? u"testpack:powder"_s : QcWeatherDefinition{}.snow.layer);
        QCOMPARE(registry.weather().lightning.damage, accepted ? 2.0 : QcWeatherDefinition{}.lightning.damage);
        if (!accepted) {
            QVERIFY2(warnings.constFirst().contains(u"snow layer"_s), qPrintable(warnings.join(u'\n')));
        }
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
        QVERIFY(!QcContentPackLoader::load(dir.path(), registry).has_value());
    }

    void atlasFallsBackToPlaceholder()
    {
        QcContentRegistry registry;
        registry.assets().registerTexture({u"a:bad"_s, QJsonObject{{u"pattern"_s, u"nope"_s}}, {}});
        registry.assets().registerTexture({u"a:png"_s, {}, QByteArray("not a png")});

        // A small file describing a huge image is refused before it is decoded.
        QByteArray huge;
        QBuffer buffer(&huge);
        buffer.open(QIODevice::WriteOnly);
        QImage(256, 8, QImage::Format_RGBA8888).save(&buffer, "PNG");
        registry.assets().registerTexture({u"a:huge"_s, {}, huge});

        const QcTextureAtlas atlas = QcTextureAtlas::build(registry);
        QCOMPARE(atlas.problems().size(), 3);
        QVERIFY(atlas.problems().join(u'\n').contains(u"at most 128"_s));
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
        registry.assets().registerTexture({u"a:green"_s, {}, png});

        const QcTextureAtlas atlas = QcTextureAtlas::build(registry);
        QVERIFY(atlas.problems().isEmpty());
        QCOMPARE(atlas.tile(u"a:green"_s).pixelColor(7, 7), QColor(10, 200, 30));
    }
};

QTEST_GUILESS_MAIN(TestContentPack)
#include "test_content_pack.moc"
