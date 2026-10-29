// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Animated textures (decision E-878): frame selection by time, the definitions' fields, the frames
// of procedural patterns, texture pack and mod strips, and the frames' tiles in the atlas.
#include "clientcore/texturepacks/QcTextureSources.h"
#include "clientcore/textures/QcAtlasLayout.h"
#include "clientcore/textures/QcProceduralTexture.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/definitions/QcTextureAnimation.h"
#include "common/content/loader/QcContentPackLoader.h"
#include "common/content/parser/QcContentParser.h"
#include "common/content/registry/QcContentRegistry.h"
#include "QcTestUtil.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QByteArray png(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// A vertical strip of square frames of `side` pixels, one plain colour each.
QImage strip(const QList<QColor> &colours, int side)
{
    QImage image(side, side * static_cast<int>(colours.size()), QImage::Format_RGBA8888);
    for (qsizetype frame = 0; frame < colours.size(); ++frame) {
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                image.setPixelColor(x, static_cast<int>(frame) * side + y, colours[frame]);
            }
        }
    }
    return image;
}

QJsonObject still(const char *primary)
{
    return {{u"pattern"_s, u"noise"_s}, {u"primary"_s, QString::fromLatin1(primary)}, {u"variation"_s, 0.0}, {u"seed"_s, 1}};
}

// SHA-1 of an image's RGBA8888 pixels, row by row without padding.
QByteArray pixelHash(const QImage &image)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (int y = 0; y < rgba.height(); ++y) {
        hash.addData(QByteArrayView(rgba.constScanLine(y), qsizetype{rgba.width()} * 4));
    }
    return hash.result().toHex();
}

// The atlas cell of a UV rectangle's centre.
QPoint cellOf(const QcTextureAtlas &atlas, const QRectF &uv)
{
    return {static_cast<int>(std::floor(uv.center().x() * atlas.animationMap().width())),
            static_cast<int>(std::floor(uv.center().y() * atlas.animationMap().height()))};
}

QcTextureAtlasOptions singleThread()
{
    QcTextureAtlasOptions options;
    options.threads = 1;
    return options;
}

void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(content), content.size());
}

QcTexturePackRef stripPack(const QString &texture, const QImage &frames, int side)
{
    auto made = std::make_shared<QcTexturePack>();
    made->id = u"strip"_s;
    made->manifest.name = made->id;
    made->manifest.resolution = side;
    QList<QImage> later;
    for (int frame = 1; frame < frames.height() / side; ++frame) {
        later.append(frames.copy(0, frame * side, side, side));
    }
    made->images.insert(texture, frames.copy(0, 0, side, side));
    made->laterFrames.insert(texture, later);
    return made;
}

} // namespace

class TestTextureAnimation : public QObject
{
    Q_OBJECT

private slots:
    void frameSelectionFollowsTheTime_data()
    {
        QTest::addColumn<double>("ticks");
        QTest::addColumn<int>("frame");
        QTest::addColumn<int>("next");
        // Four frames of two ticks: a loop of eight ticks.
        QTest::newRow("start") << 0.0 << 0 << 1;
        QTest::newRow("end of the first frame") << 1.99 << 0 << 1;
        QTest::newRow("second frame") << 2.0 << 1 << 2;
        QTest::newRow("last frame") << 7.5 << 3 << 0;
        QTest::newRow("next loop") << 8.0 << 0 << 1;
        QTest::newRow("much later") << 8000.0 + 4.0 << 2 << 3;
        QTest::newRow("before the start") << -1.0 << 3 << 0;
    }
    void frameSelectionFollowsTheTime()
    {
        QFETCH(double, ticks);
        QFETCH(int, frame);
        QFETCH(int, next);
        const QcTextureAnimation animation{.frames = 4, .frameTicks = 2, .interpolate = false};
        const QcTextureAnimation::Frame shown = animation.frameAt(ticks);
        QCOMPARE(shown.index, frame);
        QCOMPARE(shown.next, next);
        QCOMPARE(shown.blend, 0.0);
        // The same time gives the same frame every time.
        QCOMPARE(animation.frameAt(ticks).index, shown.index);
    }

    void interpolationBlendsIntoTheNextFrame()
    {
        const QcTextureAnimation animation{.frames = 3, .frameTicks = 4, .interpolate = true};
        QCOMPARE(animation.cycleTicks(), 12);
        const QcTextureAnimation::Frame quarter = animation.frameAt(5.0);
        QCOMPARE(quarter.index, 1);
        QCOMPARE(quarter.next, 2);
        QCOMPARE(quarter.blend, 0.25);
        // The last frame blends back into the first: the loop has no jump.
        const QcTextureAnimation::Frame closing = animation.frameAt(11.0);
        QCOMPARE(closing.index, 2);
        QCOMPARE(closing.next, 0);
        QCOMPARE(closing.blend, 0.75);

        // A still texture always shows its only frame.
        const QcTextureAnimation still;
        QVERIFY(!still.animated());
        QCOMPARE(still.frameAt(123.0).index, 0);
        QCOMPARE(still.frameAt(123.0).blend, 0.0);
    }

    void definitionsAreChecked_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<bool>("valid");
        QTest::newRow("still") << QByteArray("{}") << true;
        QTest::newRow("animated") << QByteArray("{\"frames\": 8, \"frameTime\": 3, \"interpolate\": true}") << true;
        QTest::newRow("one frame") << QByteArray("{\"frames\": 1}") << true;
        QTest::newRow("most frames") << QByteArray("{\"frames\": 32, \"frameTime\": 255}") << true;
        QTest::newRow("no frames") << QByteArray("{\"frames\": 0}") << false;
        QTest::newRow("too many frames") << QByteArray("{\"frames\": 33}") << false;
        QTest::newRow("part of a frame") << QByteArray("{\"frames\": 2.5}") << false;
        QTest::newRow("frames as text") << QByteArray("{\"frames\": \"4\"}") << false;
        QTest::newRow("no frame time") << QByteArray("{\"frames\": 4, \"frameTime\": 0}") << false;
        QTest::newRow("frame time too long") << QByteArray("{\"frames\": 4, \"frameTime\": 256}") << false;
        QTest::newRow("interpolate as text") << QByteArray("{\"frames\": 4, \"interpolate\": \"yes\"}") << false;
        QTest::newRow("frame time without frames") << QByteArray("{\"frameTime\": 4}") << false;
    }
    void definitionsAreChecked()
    {
        QFETCH(QByteArray, json);
        QFETCH(bool, valid);
        QCOMPARE(QcTextureAnimation::read(QJsonDocument::fromJson(json).object()).has_value(), valid);
    }

    // A mod's texture with bad animation fields is left out with a warning naming the field; good
    // ones keep their fields for the client; frames of a pattern that does not move are kept with
    // a warning.
    void badAnimationsAreRejectedWithAWarning()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(u"textures"_s));
        writeFile(dir.filePath(u"pack.json"_s), "{\"id\": \"anim\", \"version\": \"1.0\"}");
        writeFile(dir.filePath(u"textures/glow.json"_s), "{\"name\": \"glow\", \"pattern\": \"water\", \"primary\": \"#808080\", \"frames\": 4, \"frameTime\": 2}");
        writeFile(dir.filePath(u"textures/flat.json"_s), "{\"name\": \"flat\", \"pattern\": \"noise\", \"primary\": \"#808080\", \"frames\": 2}");
        writeFile(dir.filePath(u"textures/bad.json"_s), "{\"name\": \"bad\", \"pattern\": \"noise\", \"primary\": \"#808080\", \"frames\": 99}");
        writeFile(dir.filePath(u"textures/wide.json"_s),
                  "{\"name\": \"wide\", \"pattern\": \"painting_sea\", \"primary\": \"#808080\", \"tiles\": [2, 1], \"frames\": 2}");

        QcContentRegistry registry;
        const QcResult<QcLoadedPack> loaded = QcContentPackLoader::load(dir.path(), registry);
        QVERIFY(loaded.has_value());
        // In the order of the files: bad, flat, wide.
        QCOMPARE(loaded->warnings.size(), 3);
        QVERIFY2(loaded->warnings[0].contains(u"'frames'"_s), qPrintable(loaded->warnings[0]));
        QVERIFY2(loaded->warnings[1].contains(u"'noise' does not move"_s), qPrintable(loaded->warnings[1]));
        QVERIFY2(loaded->warnings[2].contains(u"'tiles'"_s), qPrintable(loaded->warnings[2]));
        QVERIFY(!registry.assets().texture(u"anim:bad"_s));
        QVERIFY(!registry.assets().texture(u"anim:wide"_s));
        QVERIFY(registry.assets().texture(u"anim:flat"_s));
        const QcTextureDefinition *glow = registry.assets().texture(u"anim:glow"_s);
        QVERIFY(glow);
        QCOMPARE(QcProceduralTexture::animationOf(glow->procedural), (QcTextureAnimation{.frames = 4, .frameTicks = 2, .interpolate = false}));
    }

    // The animated patterns move with the phase: every frame differs from the one before, the first
    // is the still texture of the same definition, so a texture without frames looks as before.
    void proceduralFramesStartWithTheStillTexture_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QByteArray>("stillHash");
        // The 16 x 16 pictures before animations (decision E-878), exported by qtcraft-headless.
        QTest::newRow("fire") << u"qtcraft:fire"_s << QByteArray("2cc878653ade1ad5695eb6a7961152139150e44d");
        QTest::newRow("lava") << u"qtcraft:lava"_s << QByteArray("a5687e89775685da82be92900fe737b332337055");
        QTest::newRow("water") << u"qtcraft:water"_s << QByteArray("b62f9ca366284bfcd43fbce764fa57947d4d2958");
    }
    void proceduralFramesStartWithTheStillTexture()
    {
        QFETCH(QString, name);
        QFETCH(QByteArray, stillHash);
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        const QcTextureDefinition *definition = content->assets().texture(name);
        QVERIFY(definition);
        const QcTextureAnimation animation = QcProceduralTexture::animationOf(definition->procedural);
        QVERIFY(animation.frames >= 8);

        const QcResult<QList<QImage>> frames = QcProceduralTexture::renderFrames(definition->procedural, 16);
        QVERIFY(frames.has_value());
        QCOMPARE(frames->size(), animation.frames);
        QCOMPARE(pixelHash(frames->front()), stillHash);
        for (qsizetype frame = 1; frame < frames->size(); ++frame) {
            QVERIFY2((*frames)[frame] != (*frames)[frame - 1], qPrintable(u"frame %1"_s.arg(frame)));
        }
        // Enlarged (texture packs with "procedural": true) every frame keeps its own picture.
        const QcResult<QImage> enlarged = QcProceduralTexture::render(definition->procedural, 32, 1);
        QVERIFY(enlarged.has_value());
        QCOMPARE(enlarged->size(), QSize(32, 32));
    }

    // QcTextureAnimation::patternMoves names exactly the patterns whose frames differ.
    void movingPatternsAreKnown_data()
    {
        QTest::addColumn<QString>("pattern");
        for (const char *pattern : {"fire", "lava", "water", "noise", "speckle", "planks", "ore", "leaves", "glass"}) {
            QTest::newRow(pattern) << QString::fromLatin1(pattern);
        }
    }
    void movingPatternsAreKnown()
    {
        QFETCH(QString, pattern);
        QJsonObject definition = still("#a06030");
        definition.insert(u"pattern"_s, pattern);
        definition.insert(u"secondary"_s, u"#ffd040"_s);
        definition.insert(u"variation"_s, 0.2);
        definition.insert(u"frames"_s, 4);
        const QcResult<QList<QImage>> frames = QcProceduralTexture::renderFrames(definition, 16);
        QVERIFY(frames.has_value());
        QCOMPARE((*frames)[0] != (*frames)[1], QcTextureAnimation::patternMoves(pattern));
    }

    // A mod's PNG strip of square frames animates up to 128 pixels wide and maxFrames frames
    // tall; wider pictures, taller ones that are no strip and longer strips are refused, the
    // longer strip with a warning that names its frames.
    void modPngStripsAnimate_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<int>("frames");
        QTest::addColumn<QString>("problem");
        QTest::newRow("square") << QSize(16, 16) << 1 << QString();
        QTest::newRow("short strip") << QSize(16, 64) << 4 << QString();
        QTest::newRow("strip taller than 128") << QSize(16, 320) << 20 << QString();
        QTest::newRow("longest strip") << QSize(32, 1024) << 32 << QString();
        QTest::newRow("wide strip") << QSize(128, 512) << 4 << QString();
        QTest::newRow("too many frames") << QSize(16, 16 * 33) << 0 << u"33 frames"_s;
        QTest::newRow("too many small frames") << QSize(2, 128) << 0 << u"64 frames"_s;
        QTest::newRow("tall, no strip") << QSize(16, 200) << 0 << u"at most 128"_s;
        QTest::newRow("too wide") << QSize(160, 160) << 0 << u"at most 128"_s;
    }
    void modPngStripsAnimate()
    {
        QFETCH(QSize, size);
        QFETCH(int, frames);
        QFETCH(QString, problem);
        QImage image(size, QImage::Format_RGBA8888);
        image.fill(Qt::darkCyan);
        const QcTextureDefinition definition{u"mod:strip"_s, {}, png(image)};
        const QcResult<QcTextureSources::Resolved> resolved = QcTextureSources::resolve(definition, {}, 16);
        if (!problem.isEmpty()) {
            QVERIFY(!resolved.has_value());
            QVERIFY2(resolved.error().contains(problem), qPrintable(resolved.error()));
            QVERIFY(!QcTextureSources::animationOf(definition, {}).animated());
            return;
        }
        QVERIFY2(resolved.has_value(), qPrintable(resolved.has_value() ? QString() : resolved.error()));
        QCOMPARE(resolved->animation.frames, frames);
        QCOMPARE(resolved->laterFrames.size(), frames - 1);
        QCOMPARE(QcTextureSources::animationOf(definition, {}).frames, frames);
    }

    // Textures without frames are drawn exactly as before animations (decision E-878).
    void stillTexturesAreUnchanged_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QByteArray>("hash");
        QTest::newRow("magma block (lava pattern)") << u"qtcraft:magma_block"_s << QByteArray("ac26a8a7f34539e97ad5364e5de2cdd937c9dc80");
        QTest::newRow("stone") << u"qtcraft:stone"_s << QByteArray("428f8ce9dcc3f8387193b0a35b5c60e6c75facac");
        QTest::newRow("oak planks") << u"qtcraft:oak_planks"_s << QByteArray("22a36c839fc7f68fde0c4b6da7e96d965abdf8c6");
        QTest::newRow("glass") << u"qtcraft:glass"_s << QByteArray("53132f3fad4881c1d48f1594635de63b4a62bd9f");
    }
    void stillTexturesAreUnchanged()
    {
        QFETCH(QString, name);
        QFETCH(QByteArray, hash);
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        const QcTextureDefinition *definition = content->assets().texture(name);
        QVERIFY(definition);
        const QcResult<QcTextureSources::Resolved> resolved = QcTextureSources::resolve(*definition, {}, 16);
        QVERIFY(resolved.has_value());
        QVERIFY(!resolved->animation.animated());
        QVERIFY(resolved->laterFrames.isEmpty());
        QCOMPARE(pixelHash(resolved->image), hash);
    }

    // Every frame is a tile of its own with its gutter: at every mip level the frame's tile and its
    // gutter show only the frame's colour, nothing of the neighbouring frames.
    void atlasHoldsEveryFrameWithoutBleeding()
    {
        QcContentRegistry content;
        content.assets().registerTexture({u"t:grey"_s, still("#808080"), {}});
        const QList<QColor> colours = {Qt::red, Qt::green, Qt::blue};
        content.assets().registerTexture({u"t:flash"_s, {}, png(strip(colours, 16))});
        content.assets().registerTexture({u"t:white"_s, still("#ffffff"), {}});

        const QcTextureAtlas atlas = QcTextureAtlas::build(content, {}, singleThread());
        QCOMPARE(atlas.animation(u"t:flash"_s).frames, 3);
        QCOMPARE(atlas.animation(u"t:flash"_s).frameTicks, QcTextureAnimation::defaultFrameTicks);
        QVERIFY(!atlas.animation(u"t:grey"_s).animated());
        QCOMPARE(atlas.frameUv(u"t:flash"_s, 0), atlas.uv(u"t:flash"_s));
        QCOMPARE(atlas.frameUv(u"t:grey"_s, 2), atlas.uv(u"t:grey"_s));

        for (qsizetype level = 0; level < atlas.levels().size(); ++level) {
            const QImage &image = atlas.levels()[level];
            for (int frame = 0; frame < 3; ++frame) {
                // The cell: the tile and its gutter of an eighth of a tile on each side.
                const QRectF uv = atlas.frameUv(u"t:flash"_s, frame);
                const double cellWidth = uv.width() * 10.0 / 8.0;
                const double cellHeight = uv.height() * 10.0 / 8.0;
                const QRect cell(static_cast<int>(std::lround((uv.x() - uv.width() / 8.0) * image.width())),
                                 static_cast<int>(std::lround((uv.y() - uv.height() / 8.0) * image.height())),
                                 static_cast<int>(std::lround(cellWidth * image.width())), static_cast<int>(std::lround(cellHeight * image.height())));
                for (int y = cell.top(); y <= cell.bottom(); ++y) {
                    for (int x = cell.left(); x <= cell.right(); ++x) {
                        QVERIFY2(image.pixelColor(x, y) == colours[frame], qPrintable(u"level %1 frame %2 pixel %3,%4"_s.arg(level).arg(frame).arg(x).arg(y)));
                    }
                }
            }
        }
    }

    // Entity meshes (flames on a burning body, decision E-919) show the frame the block shader picks
    // at the same time: frame by frame time, over the loop's end, still textures unchanged.
    void entitiesPickTheFrameOfTheBlocks()
    {
        QcContentRegistry content;
        content.assets().registerTexture({u"t:grey"_s, still("#808080"), {}});
        content.assets().registerTexture({u"t:flash"_s, {}, png(strip({Qt::red, Qt::green, Qt::blue}, 16))});
        const QcTextureAtlas atlas = QcTextureAtlas::build(content, {}, singleThread());
        const QcTextureAnimation flash = atlas.animation(u"t:flash"_s);
        QCOMPARE(flash.frameTicks, 2);

        for (const double ticks : {0.0, 1.9, 2.0, 3.5, 4.0, 5.99, 6.0, 9.0, 1000.25}) {
            const int frame = flash.frameAt(ticks).index;
            QCOMPARE(atlas.uvAt(u"t:flash"_s, ticks), atlas.frameUv(u"t:flash"_s, frame));
            QCOMPARE(atlas.uvAt(u"t:grey"_s, ticks), atlas.uv(u"t:grey"_s));
        }
        QCOMPARE(atlas.uvAt(u"t:flash"_s, 2.0), atlas.frameUv(u"t:flash"_s, 1));
        QCOMPARE(atlas.uvAt(u"t:flash"_s, 4.5), atlas.frameUv(u"t:flash"_s, 2));
        QCOMPARE(atlas.uvAt(u"t:flash"_s, 6.0), atlas.uv(u"t:flash"_s));
        QVERIFY(atlas.uvAt(u"t:flash"_s, 2.0) != atlas.uvAt(u"t:flash"_s, 0.0));
        QCOMPARE(atlas.uvAt(u"t:missing"_s, 3.0), atlas.uv(u"t:missing"_s));
    }

    // The map the block shader reads (block.vert) leads from the first frame's cell to the others:
    // frames, frame time and interpolation at that cell, the index of the second frame's tile, and
    // the later frames row by row after it.
    void animationMapLeadsToTheFrames()
    {
        QcContentRegistry content;
        for (int i = 0; i < 6; ++i) {
            content.assets().registerTexture({u"t:still%1"_s.arg(i), still("#406080"), {}});
        }
        QJsonObject water = still("#3a64d0b0");
        water.insert(u"pattern"_s, u"water"_s);
        water.insert(u"frames"_s, 5);
        water.insert(u"frameTime"_s, 3);
        water.insert(u"interpolate"_s, true);
        content.assets().registerTexture({u"t:water"_s, water, {}});

        const QcTextureAtlas atlas = QcTextureAtlas::build(content, {}, singleThread());
        const QImage &map = atlas.animationMap();
        const QcAtlasLayout layout(6 + 1 + 1 + 4);
        QCOMPARE(map.size(), QSize(layout.columns(), layout.rows()));
        const QPoint first = cellOf(atlas, atlas.uv(u"t:water"_s));
        const QColor entry = map.pixelColor(first);
        QCOMPARE(entry.red(), 5 + 128);
        QCOMPARE(entry.green(), 3);
        const int later = entry.blue() + 256 * entry.alpha();
        for (int frame = 1; frame < 5; ++frame) {
            const int index = later + frame - 1;
            QCOMPARE(cellOf(atlas, atlas.frameUv(u"t:water"_s, frame)), QPoint(index % map.width(), index / map.width()));
        }
        // Every other cell is still.
        int animatedCells = 0;
        for (int y = 0; y < map.height(); ++y) {
            for (int x = 0; x < map.width(); ++x) {
                animatedCells += map.pixelColor(x, y).red() != 0 ? 1 : 0;
            }
        }
        QCOMPARE(animatedCells, 1);
        QCOMPARE(atlas.animationCycleTicks(), 15);
    }

    // A texture pack's strip animates: over a still texture at the default frame time, over an
    // animated one at the content's time; the content's own frame count gives way to the pack's.
    void texturePackStripsAnimate()
    {
        QcTextureDefinition plain{u"t:plain"_s, still("#808080"), {}};
        QcTextureDefinition flowing{u"t:flowing"_s, still("#808080"), {}};
        flowing.procedural.insert(u"frames"_s, 8);
        flowing.procedural.insert(u"frameTime"_s, 5);
        flowing.procedural.insert(u"interpolate"_s, true);

        const QImage frames = strip({Qt::red, Qt::green}, 32);
        const QcTexturePackStack plainPack{stripPack(plain.name, frames, 32)};
        QCOMPARE(QcTextureSources::animationOf(plain, plainPack), (QcTextureAnimation{.frames = 2, .frameTicks = 2, .interpolate = false}));
        const QcResult<QcTextureSources::Resolved> resolved = QcTextureSources::resolve(plain, plainPack, 32);
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->laterFrames.size(), 1);
        QCOMPARE(resolved->laterFrames.front().pixelColor(5, 5), QColor(Qt::green));
        QCOMPARE(resolved->strip().size(), QSize(32, 64));

        const QcTexturePackStack flowingPack{stripPack(flowing.name, frames, 32)};
        QCOMPARE(QcTextureSources::animationOf(flowing, flowingPack), (QcTextureAnimation{.frames = 2, .frameTicks = 5, .interpolate = true}));
        QCOMPARE(QcTextureSources::animationOf(flowing, {}).frames, 8);
    }

    // What the animations of the base game cost in the atlas (decision E-878): logged for the
    // performance notes, and bounded.
    void baseAnimationsCostLittle()
    {
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        for (const char *name : {"qtcraft:fire", "qtcraft:lava", "qtcraft:water"}) {
            QVERIFY2(QcProceduralTexture::animationOf(content->assets().texture(QString::fromLatin1(name))->procedural).animated(), name);
        }
        // The same content with every texture still.
        QcContentRegistry stillContent = *content;
        for (const QcTextureDefinition &original : content->assets().textures()) {
            QcTextureDefinition definition = original;
            for (const QString &key : {u"frames"_s, u"frameTime"_s, u"interpolate"_s}) {
                definition.procedural.remove(key);
            }
            stillContent.assets().registerTexture(definition);
        }

        const QcTextureAtlas animated = QcTextureAtlas::build(*content, {}, singleThread());
        const QcTextureAtlas stillAtlas = QcTextureAtlas::build(stillContent, {}, singleThread());
        qInfo().noquote() << u"Atlas at 16x: %1x%2 pixels, %3 KiB with animations; %4x%5, %6 KiB without; animation map %7x%8"_s.arg(animated.image().width())
                                 .arg(animated.image().height())
                                 .arg(animated.bytes() / 1024)
                                 .arg(stillAtlas.image().width())
                                 .arg(stillAtlas.image().height())
                                 .arg(stillAtlas.bytes() / 1024)
                                 .arg(animated.animationMap().width())
                                 .arg(animated.animationMap().height());
        QVERIFY(animated.bytes() < stillAtlas.bytes() * 3 / 2);
        QVERIFY(animated.animationCycleTicks() <= QcTextureAtlas::maxCycleTicks);
    }
};

QTEST_GUILESS_MAIN(TestTextureAnimation)
#include "test_texture_animation.moc"
