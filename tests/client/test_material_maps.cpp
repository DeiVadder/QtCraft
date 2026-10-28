#include "clientcore/materials/QcMaterialAtlas.h"
#include "clientcore/materials/QcMaterialMips.h"
#include "clientcore/materials/QcMaterialRules.h"
#include "clientcore/materials/QcMaterialSynthesizer.h"
#include "clientcore/texturepacks/QcTexturePackLoader.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"
#include "QcTestUtil.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;
using Metal = QcMaterialParams::Metal;

// The material maps of the realistic mode (decision E-908): the procedural maps are deterministic
// and tile like their textures, the rules give metals, ores and light, the mip levels keep normals
// unit length and lower the smoothness where they spread, and the atlas of maps has the colour
// atlas's layout, painted maps for the game's textures, a pack's own maps and flat maps for a
// pack's image without them; a 256x pack gets 128x maps.
class TestMaterialMaps : public QObject
{
    Q_OBJECT

    // A 16 x 16 stone-like picture with a darker crack and a few brighter grains.
    static QImage stone(int size = 16)
    {
        QImage image(size, size, QImage::Format_RGBA8888);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const int grain = ((x * 7) + (y * 13)) % 5 == 0 ? 40 : 0;
                const int crack = x == size / 2 ? -50 : 0;
                const int value = 120 + grain + crack;
                image.setPixelColor(x, y, QColor(value, value, value));
            }
        }
        return image;
    }

    static QImage shifted(const QImage &image, int dx, int dy)
    {
        QImage result(image.size(), image.format());
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                result.setPixel((x + dx) % image.width(), (y + dy) % image.height(), image.pixel(x, y));
            }
        }
        return result;
    }

    static float normalLength(QRgb texel)
    {
        const float x = (static_cast<float>(qRed(texel)) / 127.5f) - 1.0f;
        const float y = (static_cast<float>(qGreen(texel)) / 127.5f) - 1.0f;
        return std::sqrt((x * x) + (y * y));
    }

private slots:
    void paintingIsDeterministicAndTiles()
    {
        const QcMaterialParams params = QcMaterialRules::forTexture(u"qtcraft:stone"_s, QJsonObject{{u"pattern"_s, u"speckle"_s}}, false);
        const QcMaterialSynthesizer::Colours colours{QColor(120, 120, 120), QColor(90, 90, 90)};
        const QcMaterialSynthesizer::Maps first = QcMaterialSynthesizer::paint(stone(), params, colours);
        const QcMaterialSynthesizer::Maps second = QcMaterialSynthesizer::paint(stone(), params, colours);
        QCOMPARE(first.normal, second.normal);
        QCOMPARE(first.specular, second.specular);

        // Neighbours wrap: the maps of a shifted picture are the shifted maps.
        const QcMaterialSynthesizer::Maps moved = QcMaterialSynthesizer::paint(shifted(stone(), 5, 11), params, colours);
        QCOMPARE(moved.normal, shifted(first.normal, 5, 11));
        QCOMPARE(moved.specular, shifted(first.specular, 5, 11));

        // The crack is lower: its sides tilt towards it.
        QVERIFY(qRed(first.normal.pixel(7, 3)) > 128);
        QVERIFY(qRed(first.normal.pixel(9, 3)) < 128);
        QVERIFY(qBlue(first.normal.pixel(8, 3)) < 255);
        QCOMPARE(qRed(first.specular.pixel(0, 0)), static_cast<int>(std::lround(params.smoothness * 255.0f)));
        QCOMPARE(qGreen(first.specular.pixel(0, 0)), 10);
        QCOMPARE(qAlpha(first.specular.pixel(0, 0)), 0);
    }

    void plainPicturesAndHolesAreFlat()
    {
        QImage plain(16, 16, QImage::Format_RGBA8888);
        plain.fill(QColor(80, 160, 40));
        plain.setPixelColor(3, 3, QColor(0, 0, 0, 0));
        const QcMaterialSynthesizer::Maps maps = QcMaterialSynthesizer::paint(plain, QcMaterialParams{}, {Qt::gray, Qt::darkGray});
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                QVERIFY(qAbs(qRed(maps.normal.pixel(x, y)) - 128) <= 1);
                QVERIFY(qAbs(qGreen(maps.normal.pixel(x, y)) - 128) <= 1);
            }
        }
        const QcMaterialSynthesizer::Maps flat = QcMaterialSynthesizer::flat(4);
        QCOMPARE(flat.normal.pixel(1, 2), QcMaterialSynthesizer::flatNormal);
        QCOMPARE(flat.specular.pixelColor(1, 2), QColor(0, 10, 0, 0));
    }

    void rulesGiveMetalsOresAndLight()
    {
        const auto rule = [](const QString &name, const QString &pattern, bool light = false) {
            return QcMaterialRules::forTexture(name, QJsonObject{{u"pattern"_s, pattern}}, light);
        };
        QCOMPARE(rule(u"qtcraft:iron_block"_s, u"metal_block"_s).metal, Metal::Iron);
        QCOMPARE(rule(u"qtcraft:gold_block"_s, u"metal_block"_s).metal, Metal::Gold);
        QCOMPARE(rule(u"qtcraft:copper_block"_s, u"metal_block"_s).metal, Metal::Copper);
        QCOMPARE(rule(u"qtcraft:diamond_block"_s, u"metal_block"_s).metal, Metal::None);
        QCOMPARE(rule(u"qtcraft:diamond_block"_s, u"metal_block"_s).reflectance, 43);
        QVERIFY(rule(u"qtcraft:glass"_s, u"glass"_s).smoothness > 0.9f);
        QCOMPARE(rule(u"qtcraft:gold_ore"_s, u"ore"_s).inclusion->metal, Metal::Gold);
        QCOMPARE(rule(u"qtcraft:torch"_s, u"torch"_s, true).emission, 1.0f);
        QCOMPARE(rule(u"qtcraft:torch"_s, u"torch"_s, false).emission, 0.0f);

        // The definition's "material" overrides; values out of range and unknown metals are ignored.
        const QJsonObject custom{{u"pattern"_s, u"speckle"_s},
                                 {u"material"_s, QJsonObject{{u"smoothness"_s, 0.7}, {u"metal"_s, u"silver"_s}, {u"bump"_s, 9.0}}}};
        const QcMaterialParams overridden = QcMaterialRules::forTexture(u"mod:thing"_s, custom, false);
        QCOMPARE(overridden.smoothness, 0.7f);
        QCOMPARE(overridden.metal, Metal::Silver);
        QCOMPARE(overridden.bump, QcMaterialRules::forTexture(u"mod:thing"_s, QJsonObject{{u"pattern"_s, u"speckle"_s}}, false).bump);
        QCOMPARE(QcMaterialParams::withOverrides({}, QJsonObject{{u"metal"_s, u"unobtainium"_s}}).metal, Metal::None);
    }

    void oreTexelsGetTheOresMaterial()
    {
        QImage ore(16, 16, QImage::Format_RGBA8888);
        ore.fill(QColor(127, 127, 127));
        ore.setPixelColor(4, 4, QColor(216, 175, 147));
        const QcMaterialParams params = QcMaterialRules::forTexture(u"qtcraft:iron_ore"_s, QJsonObject{{u"pattern"_s, u"ore"_s}}, false);
        const QcMaterialSynthesizer::Maps maps = QcMaterialSynthesizer::paint(ore, params, {QColor(0x7f7f7f), QColor(0xd8af93)});
        QCOMPARE(qGreen(maps.specular.pixel(4, 4)), static_cast<int>(Metal::Iron));
        QCOMPARE(qGreen(maps.specular.pixel(10, 10)), 10);
        QVERIFY(qRed(maps.specular.pixel(4, 4)) > qRed(maps.specular.pixel(10, 10)));
    }

    void emissionMarksTheBrightTexels()
    {
        QImage torch(16, 16, QImage::Format_RGBA8888);
        torch.fill(QColor(90, 60, 30));
        torch.setPixelColor(8, 2, QColor(255, 230, 120));
        QcMaterialParams params;
        params.emission = 1.0f;
        params.emissionTexels = 0.05f;
        const QcMaterialSynthesizer::Maps maps = QcMaterialSynthesizer::paint(torch, params, {Qt::gray, Qt::darkGray});
        QVERIFY(qAlpha(maps.specular.pixel(8, 2)) > 200);
        QCOMPARE(qAlpha(maps.specular.pixel(0, 12)), 0);
    }

    void mipLevelsKeepNormalsAndRoughenSpreadOnes()
    {
        QImage normal(2, 2, QImage::Format_RGBA8888);
        QImage specular(2, 2, QImage::Format_RGBA8888);
        specular.fill(QColor(230, 10, 0, 0));
        normal.fill(QColor::fromRgba(QcMaterialSynthesizer::flatNormal));
        QcMaterialMips::Level level = QcMaterialMips::halve(normal, specular);
        QCOMPARE(level.normal.pixel(0, 0), QcMaterialSynthesizer::flatNormal);
        QCOMPARE(qRed(level.specular.pixel(0, 0)), 230);
        QCOMPARE(qAlpha(level.specular.pixel(0, 0)), 0);

        // Normals that tilt apart: the average points straight up again but the surface is rougher.
        normal.setPixelColor(0, 0, QColor(230, 128, 200));
        normal.setPixelColor(1, 0, QColor(26, 128, 200));
        normal.setPixelColor(0, 1, QColor(128, 230, 200));
        normal.setPixelColor(1, 1, QColor(128, 26, 200));
        specular.setPixelColor(0, 0, QColor(230, 10, 0, 200));
        level = QcMaterialMips::halve(normal, specular);
        QVERIFY(normalLength(level.normal.pixel(0, 0)) < 0.02f);
        QVERIFY(qRed(level.specular.pixel(0, 0)) < 200);
        // One of four texels glows: a quarter of its light (0 is none in the atlas).
        QCOMPARE(qAlpha(level.specular.pixel(0, 0)), 50);
    }

    void atlasHasTheColourLayoutAndTheGamesMaps()
    {
        const auto content = QcTestUtil::baseContent();
        QcTextureAtlas::Options options;
        options.materials = true;
        const QcTextureAtlas atlas = QcTextureAtlas::build(*content->registry, {}, options);
        const QcMaterialAtlas maps = QcMaterialAtlas::build(atlas, *content->registry, {});
        QCOMPARE(maps.normalLevels().size(), atlas.levels().size());
        QCOMPARE(maps.normalLevels().front().size(), atlas.image().size());
        QCOMPARE(maps.specularLevels().back().size(), atlas.levels().back().size());
        QVERIFY(atlas.tileSources().size() > content->registry->assets().textures().size());
        QVERIFY(atlas.tileSources().size() <= qsizetype{atlas.layout().columns()} * atlas.layout().rows());

        const QImage iron = maps.tile(maps.specularLevels(), atlas.uv(u"qtcraft:iron_block"_s));
        QCOMPARE(qGreen(iron.pixel(5, 5)), static_cast<int>(Metal::Iron));
        const QImage gold = maps.tile(maps.specularLevels(), atlas.uv(u"qtcraft:gold_ore"_s));
        bool goldTexel = false;
        for (int y = 0; y < gold.height(); ++y) {
            for (int x = 0; x < gold.width(); ++x) {
                goldTexel = goldTexel || qGreen(gold.pixel(x, y)) == static_cast<int>(Metal::Gold);
            }
        }
        QVERIFY(goldTexel);
        const QImage torch = maps.tile(maps.specularLevels(), atlas.uv(u"qtcraft:torch"_s));
        bool glowing = false;
        for (int y = 0; y < torch.height(); ++y) {
            for (int x = 0; x < torch.width(); ++x) {
                glowing = glowing || qAlpha(torch.pixel(x, y)) > 0;
            }
        }
        QVERIFY(glowing);
    }

    void packImagesBringTheirMapsOrFlatOnes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QDir pack(directory.path());
        QVERIFY(pack.mkpath(u"textures/qtcraft"_s));
        QFile manifest(pack.filePath(u"texturepack.json"_s));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(QJsonDocument(QJsonObject{{u"format"_s, 1}, {u"name"_s, u"Maps"_s}, {u"resolution"_s, 16}}).toJson());
        manifest.close();
        QImage colour(16, 16, QImage::Format_RGBA8888);
        colour.fill(QColor(100, 100, 100));
        QImage normal(16, 16, QImage::Format_RGBA8888);
        normal.fill(QColor(200, 60, 240, 255));
        QImage specular(16, 16, QImage::Format_RGBA8888);
        specular.fill(QColor(180, 231, 0, 255));
        QVERIFY(colour.save(pack.filePath(u"textures/qtcraft/stone.png"_s)));
        QVERIFY(normal.save(pack.filePath(u"textures/qtcraft/stone_n.png"_s)));
        QVERIFY(specular.save(pack.filePath(u"textures/qtcraft/stone_s.png"_s)));
        QVERIFY(colour.save(pack.filePath(u"textures/qtcraft/dirt.png"_s)));

        // Without the realistic mode's maps the loader does not read them.
        const auto withoutMaps = QcTexturePackLoader::load(directory.path(), u"user:maps"_s, QcTexturePack::Origin::User);
        QVERIFY(withoutMaps);
        QVERIFY(withoutMaps->normalMaps.isEmpty());
        // ... but counts them: the pack is read again when the maps are wanted (decision E-912).
        QCOMPARE(withoutMaps->unreadMaps, 2);
        QVERIFY(!withoutMaps->holdsMaps());
        QVERIFY(withoutMaps->images.contains(u"qtcraft:stone"_s));
        QcTexturePackLoader::Limits limits;
        limits.materialMaps = true;
        const auto loaded = QcTexturePackLoader::load(directory.path(), u"user:maps"_s, QcTexturePack::Origin::User, limits);
        QVERIFY(loaded);
        QVERIFY(loaded->images.contains(u"qtcraft:stone"_s));
        QVERIFY(!loaded->images.contains(u"qtcraft:stone_n"_s));
        QCOMPARE(loaded->normalMaps.value(u"qtcraft:stone"_s).pixelColor(0, 0), QColor(200, 60, 240));
        QCOMPARE(loaded->specularMaps.value(u"qtcraft:stone"_s).pixelColor(0, 0), QColor(180, 231, 0));
        QVERIFY(loaded->holdsMaps());
        QCOMPARE(loaded->unreadMaps, 0);

        const auto content = QcTestUtil::baseContent();
        QcTextureAtlas::Options options;
        options.materials = true;
        const QcTexturePackStack packs{std::make_shared<const QcTexturePack>(*loaded)};
        const QcTextureAtlas atlas = QcTextureAtlas::build(*content->registry, packs, options);
        const QcMaterialAtlas maps = QcMaterialAtlas::build(atlas, *content->registry, packs);
        QCOMPARE(maps.tile(maps.normalLevels(), atlas.uv(u"qtcraft:stone"_s)).pixelColor(3, 3), QColor(200, 60, 240));
        // labPBR's 255 (no light) is 0 in the atlas.
        QCOMPARE(maps.tile(maps.specularLevels(), atlas.uv(u"qtcraft:stone"_s)).pixelColor(3, 3), QColor(180, 231, 0, 0));
        QCOMPARE(maps.tile(maps.normalLevels(), atlas.uv(u"qtcraft:dirt"_s)).pixel(3, 3), QcMaterialSynthesizer::flatNormal);
        QCOMPARE(maps.tile(maps.specularLevels(), atlas.uv(u"qtcraft:dirt"_s)).pixelColor(3, 3), QColor(0, 10, 0, 0));
    }

    void largePacksGetMapsOfAtMost128()
    {
        auto pack = std::make_shared<QcTexturePack>();
        pack->id = u"user:large"_s;
        pack->manifest.resolution = 256;
        QImage stoneImage(256, 256, QImage::Format_RGBA8888);
        stoneImage.fill(QColor(120, 120, 120));
        pack->images.insert(u"qtcraft:stone"_s, stoneImage);
        const auto content = QcTestUtil::baseContent();
        QcTextureAtlas::Options options;
        options.materials = true;
        options.memoryBudget = qint64{1} << 30;
        options.maxExtent = 16384;
        const QcTextureAtlas atlas = QcTextureAtlas::build(*content->registry, {pack}, options);
        QCOMPARE(atlas.tileSize(), 256);
        const QcMaterialAtlas maps = QcMaterialAtlas::build(atlas, *content->registry, {pack});
        QCOMPARE(maps.tileSize(), QcMaterialAtlas::maxTileSize);
        QCOMPARE(maps.normalLevels().front().width() * 2, atlas.image().width());
    }

    // Maps come after the colour textures with their own limit: a pack full of maps keeps all its
    // colour textures; maps above 128 are box-filtered down without touching their channels.
    void mapsNeverPushColourTexturesOut()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QDir pack(directory.path());
        QVERIFY(pack.mkpath(u"textures/qtcraft"_s));
        QFile manifest(pack.filePath(u"texturepack.json"_s));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(QJsonDocument(QJsonObject{{u"format"_s, 1}, {u"name"_s, u"Big"_s}, {u"resolution"_s, 256}}).toJson());
        manifest.close();
        QImage colour(256, 256, QImage::Format_RGBA8888);
        colour.fill(QColor(100, 100, 100));
        QImage specular(256, 256, QImage::Format_RGBA8888);
        // Height-like alpha near 0 must not wipe the other channels (premultiplied scaling would).
        specular.fill(QColor(200, 231, 40, 3));
        for (const QString &name : {u"a"_s, u"b"_s, u"c"_s}) {
            QVERIFY(colour.save(pack.filePath(u"textures/qtcraft/%1.png"_s.arg(name))));
            QVERIFY(specular.save(pack.filePath(u"textures/qtcraft/%1_s.png"_s.arg(name))));
        }
        QcTexturePackLoader::Limits limits;
        limits.materialMaps = true;
        limits.maxFiles = 3;
        limits.maxMapFiles = 2;
        const auto loaded = QcTexturePackLoader::load(directory.path(), u"user:big"_s, QcTexturePack::Origin::User, limits);
        QVERIFY(loaded);
        QCOMPARE(loaded->images.size(), 3);
        QCOMPARE(loaded->specularMaps.size(), 2);
        const QImage stored = loaded->specularMaps.value(u"qtcraft:a"_s);
        QCOMPARE(stored.size(), QSize(128, 128));
        QCOMPARE(stored.pixelColor(10, 10), QColor(200, 231, 40, 3));
    }

    // A pack image taken as a map whose name is also a texture of the content is reported.
    void mapNamesThatAreTexturesAreReported()
    {
        auto pack = std::make_shared<QcTexturePack>();
        pack->id = u"user:clash"_s;
        QImage map(16, 16, QImage::Format_RGBA8888);
        map.fill(Qt::gray);
        pack->normalMaps.insert(u"mod:sign"_s, map);
        pack->specularMaps.insert(u"mod:stone"_s, map);
        const QStringList clashes = QcMaterialAtlas::clashes({u"mod:sign_n"_s, u"mod:stone"_s}, {pack});
        QCOMPARE(clashes.size(), 1);
        QVERIFY(clashes.front().contains(u"mod:sign_n"_s));
    }
};

QTEST_GUILESS_MAIN(TestMaterialMaps)
#include "test_material_maps.moc"
