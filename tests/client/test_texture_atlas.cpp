#include "clientcore/meshing/QcEntityMesher.h"
#include "clientcore/session/QcClientEntity.h"
#include "clientcore/texturepacks/QcTexturePackLibrary.h"
#include "clientcore/texturepacks/QcTextureSources.h"
#include "clientcore/texturepacks/QcTextureStackBuild.h"
#include "clientcore/textures/QcAlphaCoverage.h"
#include "clientcore/textures/QcProceduralTexture.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "clientcore/textures/QcTextureDetail.h"
#include "clientcore/textures/QcTextureSynthesizer.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcContentRegistry.h"
#include "QcTestUtil.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrentRun>
#include <QJsonObject>
#include <QTest>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

QImage filled(int size, const QColor &colour)
{
    QImage image(size, size, QImage::Format_RGBA8888);
    image.fill(colour);
    return image;
}

QByteArray png(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QJsonObject procedural(const char *pattern, const char *primary, const char *secondary = "#202020")
{
    return {{u"pattern"_s, QString::fromLatin1(pattern)},
            {u"primary"_s, QString::fromLatin1(primary)},
            {u"secondary"_s, QString::fromLatin1(secondary)},
            {u"seed"_s, 7}};
}

QJsonObject flat(const char *primary)
{
    QJsonObject spec = procedural("noise", primary);
    spec.insert(u"variation"_s, 0.0);
    spec.insert(u"detail"_s, QJsonObject{{u"grain"_s, 0.0}, {u"bevel"_s, 0.0}});
    return spec;
}

// A pack made in memory: images and specs by texture name.
QcTexturePackRef pack(const QString &id, int resolution, const QHash<QString, QImage> &images, const QHash<QString, QJsonObject> &specs = {},
                      bool proceduralContent = false)
{
    auto made = std::make_shared<QcTexturePack>();
    made->id = id;
    made->manifest.name = id;
    made->manifest.resolution = resolution;
    made->manifest.procedural = proceduralContent;
    made->images = images;
    made->specs = specs;
    return made;
}

// Content with a few procedural textures and one PNG texture.
QcContentRegistry smallContent()
{
    QcContentRegistry content;
    content.assets().registerTexture({u"t:red"_s, procedural("noise", "#c00000"), {}});
    content.assets().registerTexture({u"t:green"_s, procedural("noise", "#00c000"), {}});
    content.assets().registerTexture({u"t:blue"_s, procedural("noise", "#0000c0"), {}});
    content.assets().registerTexture({u"t:picture"_s, {}, png(filled(16, QColor(200, 200, 0)))});
    return content;
}

QColor centre(const QcTextureAtlas &atlas, const QString &name)
{
    const QImage tile = atlas.tile(name);
    return tile.pixelColor(tile.width() / 2, tile.height() / 2);
}

// Mean absolute difference per channel of two images of the same size.
double difference(const QImage &first, const QImage &second)
{
    double sum = 0.0;
    for (int y = 0; y < first.height(); ++y) {
        for (int x = 0; x < first.width(); ++x) {
            const QColor a = first.pixelColor(x, y);
            const QColor b = second.pixelColor(x, y);
            sum += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue()) + std::abs(a.alpha() - b.alpha());
        }
    }
    return sum / (4.0 * first.width() * first.height());
}

QcTextureStackBuild::Request stackRequest(const QStringList &ids, const QList<QcTexturePackLibrary::Entry> &entries,
                                          const QHash<QString, QcTexturePackRef> &loaded, std::shared_ptr<const QcContentRegistry> content)
{
    QcTextureStackBuild::Request request;
    request.ids = ids;
    request.entries = entries;
    request.loaded = loaded;
    request.content = std::move(content);
    return request;
}

// Writes an image pack with one texture in `colour` into `directory`.
bool writePack(const QString &directory, int resolution, const QString &texture, const QColor &colour)
{
    QDir().mkpath(directory + u"/textures/t"_s);
    QFile manifest(directory + u"/texturepack.json"_s);
    const QByteArray json = QJsonDocument(QJsonObject{{u"format"_s, 1}, {u"name"_s, u"Pack"_s}, {u"resolution"_s, resolution}}).toJson();
    return manifest.open(QIODevice::WriteOnly) && manifest.write(json) == json.size()
           && filled(resolution, colour).save(directory + u"/textures/t/"_s + texture + u".png"_s);
}

} // namespace

class TestTextureAtlas : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Never the player's own data folder.
        QVERIFY(m_data.isValid());
        qputenv("QTCRAFT_DATA_DIR", m_data.path().toUtf8());
    }

    void fallbackFollowsThePackOrder()
    {
        const QcContentRegistry content = smallContent();
        const QcTexturePackRef high = pack(u"high"_s, 32, {{u"t:red"_s, filled(32, Qt::white)}});
        const QcTexturePackRef low = pack(u"low"_s, 32, {{u"t:red"_s, filled(32, Qt::black)}, {u"t:green"_s, filled(32, Qt::black)}},
                                          {{u"t:picture"_s, flat("#123456")}});
        const QcTextureAtlas atlas = QcTextureAtlas::build(content, {high, low});

        QCOMPARE(atlas.tileSize(), 32);
        // The first pack wins, a texture it lacks comes from the next one, the rest from the content.
        QCOMPARE(centre(atlas, u"t:red"_s), QColor(Qt::white));
        QCOMPARE(centre(atlas, u"t:green"_s), QColor(Qt::black));
        QCOMPARE(centre(atlas, u"t:picture"_s).blue(), 0x56);
        QCOMPARE(centre(atlas, u"t:blue"_s).blue() > 150 && centre(atlas, u"t:blue"_s).red() < 60, true);
        QCOMPARE(atlas.sources().value(u"high"_s), 1);
        QCOMPARE(atlas.sources().value(u"low"_s), 2);
        QCOMPARE(atlas.sources().value(QString()), 1);

        // Swapping the order swaps the winner.
        QCOMPARE(centre(QcTextureAtlas::build(content, {low, high}), u"t:red"_s), QColor(Qt::black));
    }

    void imageBeforeSpecWithinOnePack()
    {
        const QcTextureDefinition definition{u"t:red"_s, procedural("noise", "#c00000"), {}};
        const QcTexturePackRef both = pack(u"both"_s, 16, {{u"t:red"_s, filled(16, Qt::white)}}, {{u"t:red"_s, procedural("noise", "#000000")}});
        const auto resolved = QcTextureSources::resolve(definition, {both}, 16);
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->pack, u"both"_s);
        QCOMPARE(resolved->image.pixelColor(3, 3), QColor(Qt::white));
    }

    void proceduralPackRendersContentSpecsButNotContentImages()
    {
        const QcContentRegistry content = smallContent();
        const QcTexturePackRef hd = pack(u"hd"_s, 64, {}, {}, true);
        const QcTexturePackRef images = pack(u"images"_s, 32, {{u"t:green"_s, filled(32, Qt::white)}, {u"t:picture"_s, filled(32, Qt::white)}});

        const QcTextureAtlas atlas = QcTextureAtlas::build(content, {hd, images});
        QCOMPARE(atlas.tileSize(), 64);
        // The procedural pack comes first and draws every procedural texture of the content ...
        QCOMPARE(atlas.sources().value(u"hd"_s), 3);
        QVERIFY(centre(atlas, u"t:green"_s).green() > 150);
        // ... but a content PNG is no spec: it falls through to the image pack.
        QCOMPARE(centre(atlas, u"t:picture"_s), QColor(Qt::white));

        // Below an image pack, the image pack wins where it has a file.
        const QcTextureAtlas below = QcTextureAtlas::build(content, {images, hd});
        QCOMPARE(centre(below, u"t:green"_s), QColor(Qt::white));
        QCOMPARE(below.sources().value(u"hd"_s), 2);
    }

    void contentImagesKeepTheirPixelsAtEveryResolution()
    {
        const QcContentRegistry content = smallContent();
        const QcTextureAtlas base = QcTextureAtlas::build(content);
        QCOMPARE(base.tileSize(), 16);
        QCOMPARE(centre(base, u"t:picture"_s), QColor(200, 200, 0));
        const QcTextureAtlas large = QcTextureAtlas::build(content, {pack(u"empty"_s, 128, {})});
        QCOMPARE(large.tileSize(), 128);
        QCOMPARE(centre(large, u"t:picture"_s), QColor(200, 200, 0));
        // Without a procedural pack the content's procedural textures are only enlarged pixel by pixel.
        const QImage small = base.tile(u"t:red"_s);
        const QImage enlarged = large.tile(u"t:red"_s);
        QCOMPARE(enlarged.pixelColor(8 * 3 + 2, 8 * 5 + 7), small.pixelColor(3, 5));
    }

    void layoutIsTheSameAtEveryTileSize()
    {
        const QcContentRegistry content = smallContent();
        const QcTextureAtlas base = QcTextureAtlas::build(content);
        for (const int resolution : {32, 64, 128, 256}) {
            const QcTextureAtlas atlas = QcTextureAtlas::build(content, {pack(u"p"_s, resolution, {})});
            QCOMPARE(atlas.tileSize(), resolution);
            QVERIFY(atlas.sameLayout(base));
            // 5 tiles (with the placeholder) in a 3 x 2 grid of cells of 1.25 tiles.
            QCOMPARE(atlas.image().size(), QSize(3 * resolution * 5 / 4, 2 * resolution * 5 / 4));
            QCOMPARE(atlas.levels().size(), QcAtlasLayout::levelCount(resolution));
            QCOMPARE(atlas.levels().back().width() * (1 << (atlas.levels().size() - 1)), atlas.image().width());
            // The block shader's filtering finds the grid from the last level (decision E-721).
            QCOMPARE(QcAtlasLayout::gridOf(atlas.levels().back().size()), QSize(3, 2));
        }
        QCOMPARE(QcAtlasLayout::gridOf(base.levels().back().size()), QSize(3, 2));
        QCOMPARE(QcAtlasLayout::levelCount(16), 2);
        QCOMPARE(QcAtlasLayout::levelCount(64), 4);
        QCOMPARE(QcAtlasLayout::levelCount(256), 6);
    }

    void gutterRepeatsTheEdgeAndMipsDoNotBleed_data()
    {
        QTest::addColumn<int>("resolution");
        QTest::addColumn<int>("extra");
        QTest::newRow("16x, square grid") << 16 << 0;
        QTest::newRow("64x, square grid") << 64 << 0;
        QTest::newRow("16x, 3 x 2 grid") << 16 << 2;
        QTest::newRow("128x, 3 x 2 grid") << 128 << 2;
    }

    void gutterRepeatsTheEdgeAndMipsDoNotBleed()
    {
        QFETCH(int, resolution);
        QFETCH(int, extra);
        QcContentRegistry content;
        content.assets().registerTexture({u"t:red"_s, {}, png(filled(16, Qt::red))});
        content.assets().registerTexture({u"t:blue"_s, {}, png(filled(16, Qt::blue))});
        content.assets().registerTexture({u"t:green"_s, {}, png(filled(16, Qt::green))});
        for (int i = 0; i < extra; ++i) {
            content.assets().registerTexture({u"t:white%1"_s.arg(i), {}, png(filled(16, Qt::white))});
        }
        const QcTextureAtlas atlas = QcTextureAtlas::build(content, {pack(u"p"_s, resolution, {})});
        QCOMPARE(atlas.image().width() != atlas.image().height(), extra > 0);

        for (qsizetype level = 0; level < atlas.levels().size(); ++level) {
            const QImage &image = atlas.levels()[level];
            for (const auto &[name, colour] : {std::pair{u"t:red"_s, QColor(Qt::red)}, {u"t:blue"_s, QColor(Qt::blue)}}) {
                const QRectF uv = atlas.uv(name);
                const int tile = resolution >> level;
                const int gutter = tile / 8;
                const int x0 = int(std::lround(uv.x() * image.width()));
                const int y0 = int(std::lround(uv.y() * image.height()));
                // The whole cell, gutter included, has the tile's colour.
                for (const QPoint p : {QPoint(x0 - gutter, y0 - gutter), QPoint(x0 + tile + gutter - 1, y0 + tile + gutter - 1),
                                       QPoint(x0 - gutter, y0 + tile - 1), QPoint(x0 + tile / 2, y0 + tile / 2)}) {
                    QVERIFY2(image.pixelColor(p) == colour, qPrintable(u"level %1 %2 at %3,%4"_s.arg(level).arg(name).arg(p.x()).arg(p.y())));
                }
            }
        }
    }

    // A thin cut-out shape keeps being drawn in the small mip levels: a 1-pixel line in a 128 x 128
    // tile would fade to alpha 1/16 at 8 pixels, below the shader's cut-off, without the coverage
    // scaling. Opaque and translucent tiles keep their averaged alpha.
    void thinCutOutShapesSurviveSmallMipLevels()
    {
        QImage line(128, 128, QImage::Format_RGBA8888);
        line.fill(Qt::transparent);
        for (int y = 0; y < 128; ++y) {
            line.setPixelColor(64, y, Qt::white);
        }
        QImage glass(16, 16, QImage::Format_RGBA8888);
        glass.fill(QColor(200, 220, 255, 60));
        QcContentRegistry content;
        content.assets().registerTexture({u"t:line"_s, {}, png(line)});
        content.assets().registerTexture({u"t:glass"_s, {}, png(glass)});
        const QcTextureAtlas atlas = QcTextureAtlas::build(content, {pack(u"p"_s, 128, {})});
        QCOMPARE(atlas.levels().size(), 5);

        const QImage &last = atlas.levels().back();
        const QRectF uv = atlas.uv(u"t:line"_s);
        const QPoint origin(int(std::lround(uv.x() * last.width())), int(std::lround(uv.y() * last.height())));
        int drawn = 0;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                drawn += last.pixelColor(origin + QPoint(x, y)).alpha() >= 26;
            }
        }
        // One column of eight texels: the share of level 0 (1/128 rounds up to one column).
        QCOMPARE(drawn, 8);

        const QRectF glassUv = atlas.uv(u"t:glass"_s);
        QCOMPARE(last.pixelColor(int(std::lround(glassUv.x() * last.width())) + 3, int(std::lround(glassUv.y() * last.height())) + 3).alpha(), 60);
    }

    void mipLevelsWeighColoursByAlpha()
    {
        QImage level(2, 2, QImage::Format_RGBA8888);
        // Transparent white: its colour must not count.
        level.fill(QColor(255, 255, 255, 0));
        level.setPixelColor(0, 0, QColor(200, 0, 0, 255));
        const QImage next = QcAtlasLayout::halve(level);
        QCOMPARE(next.size(), QSize(1, 1));
        // A transparent pixel does not pull the colour towards black.
        QCOMPARE(next.pixelColor(0, 0), QColor(200, 0, 0, 64));
    }

    void gpuLimitAndBudgetLowerTheTileSize()
    {
        const QcContentRegistry content = smallContent();
        const QcTexturePackRef big = pack(u"big"_s, 256, {});

        QcTextureAtlas::Options gpu;
        gpu.maxExtent = 500;
        const QcTextureAtlas limited = QcTextureAtlas::build(content, {big}, gpu);
        // 3 cells of 1.25 tiles must fit 500 pixels: 128 is too large (480 fits, 960 does not)...
        QCOMPARE(limited.requestedTileSize(), 256);
        QCOMPARE(limited.tileSize(), 128);
        QVERIFY(limited.image().width() <= 500);
        const QcTexturePackProblems notices = limited.notices();
        QCOMPARE(notices.size(), 1);
        QVERIFY(notices.front().englishText().contains(u"shown at 128x instead of 256x"_s));
        QVERIFY(limited.sameLayout(QcTextureAtlas::build(content)));

        QcTextureAtlas::Options memory;
        memory.memoryBudget = qint64(200) * 1024;
        const QcTextureAtlas budget = QcTextureAtlas::build(content, {big}, memory);
        QVERIFY(budget.bytes() <= memory.memoryBudget);
        QCOMPARE(budget.tileSize(), 64);

        // The renderer picks the first level that fits its own limit.
        QCOMPARE(QcAtlasLayout::firstLevelWithin(limited.levels(), 10000), 0);
        QCOMPARE(QcAtlasLayout::firstLevelWithin(limited.levels(), limited.image().width() / 2), 1);
        QCOMPARE(QcAtlasLayout::firstLevelWithin(limited.levels(), 1), -1);
        QVERIFY(QcTextureAtlas::build(content, {big}).notices().isEmpty());
    }

    // A 512x pack is shown at 256x (the largest stored size) and the player is told so.
    void packsAbove256AreShownAt256WithANotice()
    {
        const QcTextureAtlas atlas = QcTextureAtlas::build(smallContent(), {pack(u"huge"_s, 512, {})});
        QCOMPARE(atlas.requestedTileSize(), 512);
        QCOMPARE(atlas.tileSize(), 256);
        const QcTexturePackProblems notices = atlas.notices();
        QCOMPARE(notices.size(), 1);
        QVERIFY(notices.front().englishText().contains(u"shown at 256x instead of 512x"_s));

        // Atlases for icons only are capped silently.
        QcTextureAtlas::Options icons;
        icons.maxTileSize = 32;
        const QcTextureAtlas small = QcTextureAtlas::build(smallContent(), {pack(u"huge"_s, 512, {})}, icons);
        QCOMPARE(small.tileSize(), 32);
        QVERIFY(small.notices().isEmpty());
    }

    // A procedural pack is painted at the tile size when that is smaller than its own resolution,
    // not painted large and shrunk.
    void proceduralPacksPaintAtTheTileSize()
    {
        QcContentRegistry content;
        content.assets().registerTexture({u"t:ore"_s, procedural("ore", "#808080", "#d0a040"), {}});
        QcTextureAtlas::Options options;
        options.maxTileSize = 32;
        const QcTextureAtlas atlas = QcTextureAtlas::build(content, {pack(u"hd"_s, 128, {}, {}, true)}, options);
        QCOMPARE(atlas.tileSize(), 32);
        QCOMPARE(atlas.tile(u"t:ore"_s), *QcProceduralTexture::render(procedural("ore", "#808080", "#d0a040"), 32));
    }

    // The tiles are drawn by several threads; one thread gives the same atlas.
    void oneThreadDrawsTheSameAtlasAsMany()
    {
        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        const QcTexturePackRef hd = pack(u"hd"_s, 32, {}, {}, true);
        QcTextureAtlas::Options one;
        one.threads = 1;
        QcTextureAtlas::Options many;
        many.threads = 6;
        QCOMPARE(QcTextureAtlas::build(*content->registry, {hd}, one).image(), QcTextureAtlas::build(*content->registry, {hd}, many).image());
    }

    // Without packs every tile of the game is exactly the 16 x 16 picture of before the stage
    // (pattern code and content definitions are unchanged since dev 7c7c924; the old atlas drew
    // QcTextureSynthesizer::synthesize of the same spec). Only the colour of fully transparent
    // texels differs: since E-332 they take the nearest drawn colour (invisible at level 0).
    void baseTilesAreThePlainSynthesizedTextures()
    {
        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        const QcTextureAtlas atlas = QcTextureAtlas::build(*content->registry);
        QCOMPARE(atlas.tileSize(), 16);
        for (const QcTextureDefinition &definition : content->registry->assets().textures()) {
            const QcResult<QcTextureSpec> spec = QcProceduralTexture::spec(definition.procedural);
            QVERIFY2(spec.has_value(), qPrintable(definition.name));
            QImage synthesized = QcTextureSynthesizer::synthesize(*spec).convertToFormat(QImage::Format_RGBA8888);
            QcAlphaCoverage::fillTransparentColour(synthesized);
            QVERIFY2(atlas.tile(definition.name) == synthesized, qPrintable(definition.name));
        }
    }

    // Blocks of the opaque layer are drawn without alpha test (decision E-756): none of their
    // textures may have a transparent texel, else it would show in its colour.
    void opaqueBlocksHaveNoTransparentTexels()
    {
        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        const QcTextureAtlas atlas = QcTextureAtlas::build(*content->registry);
        const auto hasTransparentTexel = [&atlas](const QString &texture) {
            const QImage tile = atlas.tile(texture).convertToFormat(QImage::Format_RGBA8888);
            for (int y = 0; y < tile.height(); ++y) {
                for (int x = 0; x < tile.width(); ++x) {
                    if (tile.pixelColor(x, y).alpha() < QcAlphaCoverage::cutoff) {
                        return true;
                    }
                }
            }
            return false;
        };

        int checked = 0;
        for (const QcBlockDefinition &block : content->registry->blocks()) {
            if (block.renderLayer != QcRenderLayer::Opaque) {
                continue;
            }
            QStringList textures(block.faceTextures.begin(), block.faceTextures.end());
            for (const auto &[slot, texture] : block.textureSlots) {
                textures.append(texture);
            }
            for (const QString &texture : textures) {
                if (texture.isEmpty()) {
                    continue;
                }
                ++checked;
                QVERIFY2(!hasTransparentTexel(texture), qPrintable(block.name + u": "_s + texture));
            }
        }
        QVERIFY(checked > 100);
        QVERIFY2(atlas.problems().isEmpty(), qPrintable(atlas.problems().join(u"; "_s)));

        // The atlas reports the same for a block definition in the game and in the log; every
        // bundled HD pack keeps the opaque textures opaque too.
        QcBlockDefinition holed = content->registry->block(*content->registry->blockId(u"qtcraft:stone"_s));
        holed.name = u"t:holed"_s;
        holed.faceTextures.fill(u"qtcraft:oak_leaves"_s);
        QcContentRegistry withHole = *content->registry;
        QVERIFY(withHole.registerBlock(holed).has_value());
        const QStringList problems = QcTextureAtlas::build(withHole).problems();
        QCOMPARE(problems.size(), 1);
        QVERIFY2(problems.front().contains(u"t:holed"_s) && problems.front().contains(u"transparent texels"_s), qPrintable(problems.front()));
        const QList<QcTexturePackLibrary::Entry> entries = QcTexturePackLibrary::scan(QcTexturePackLibrary::roots({}));
        for (const QString &id : {u"builtin:qtcraft_hd_32"_s, u"builtin:qtcraft_hd_64"_s, u"builtin:qtcraft_hd_128"_s, u"builtin:storybook_32"_s}) {
            const QcTextureStackBuild::Result built = QcTextureStackBuild::run(stackRequest({id}, entries, {}, content->registry));
            QVERIFY2(built.atlas, qPrintable(id));
            QVERIFY2(built.atlas->problems().isEmpty(), qPrintable(id + u": "_s + built.atlas->problems().join(u"; "_s)));
        }
    }

    // Other content (other textures) means other UVs: then the game makes a new render table and
    // meshes (QcWorldAtlas::apply); the same content never does.
    void otherContentChangesTheLayout()
    {
        QcContentRegistry more = smallContent();
        more.assets().registerTexture({u"t:extra"_s, procedural("noise", "#ffffff"), {}});
        const QcTextureAtlas base = QcTextureAtlas::build(smallContent());
        QVERIFY(!QcTextureAtlas::build(more).sameLayout(base));
        QVERIFY(QcTextureAtlas::build(smallContent(), {pack(u"p"_s, 128, {})}).sameLayout(base));
    }

    void tilesForIconsComeFromASmallerLevel()
    {
        const QcContentRegistry content = smallContent();
        const QcTextureAtlas atlas = QcTextureAtlas::build(content, {pack(u"p"_s, 128, {})});
        QCOMPARE(atlas.tile(u"t:red"_s).size(), QSize(128, 128));
        QCOMPARE(atlas.tile(u"t:red"_s, 64).size(), QSize(64, 64));
        QCOMPARE(atlas.tile(u"t:red"_s, 40).size(), QSize(32, 32));
        QCOMPARE(atlas.levelForTile(32).width(), atlas.image().width() / 4);
        QCOMPARE(atlas.tile(u"t:picture"_s, 32).pixelColor(16, 16), QColor(200, 200, 0));
    }

    void proceduralRenderingIsDeterministic()
    {
        const QJsonObject ore = procedural("ore", "#808080", "#d0a040");
        for (const int resolution : {16, 32, 64, 128}) {
            const QcResult<QImage> first = QcProceduralTexture::render(ore, resolution);
            QVERIFY(first.has_value());
            QCOMPARE(first->size(), QSize(resolution, resolution));
            QCOMPARE(*first, *QcProceduralTexture::render(ore, resolution));
        }

        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        const QcTexturePackRef hd = pack(u"hd"_s, 32, {}, {}, true);
        const QcTextureAtlas first = QcTextureAtlas::build(*content->registry, {hd});
        QCOMPARE(first.sources().value(u"hd"_s), content->registry->assets().textures().size());
        QCOMPARE(first.image(), QcTextureAtlas::build(*content->registry, {hd}).image());
    }

    // More detail without blurring: the enlarged picture keeps the 16 x 16 picture's colours on
    // average, but adds pixels inside the 16 x 16 cells, and a cut-out icon keeps hard edges.
    void enlargingAddsDetailWithoutBlur()
    {
        const QJsonObject bricks = procedural("bricks", "#a04030", "#d0d0c0");
        const QImage base = *QcProceduralTexture::render(bricks, 16);
        const QImage large = *QcProceduralTexture::render(bricks, 64);
        const QImage blocky = base.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        QVERIFY(large != blocky);
        QVERIFY2(difference(large.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), base) < 12.0,
                 qPrintable(QString::number(difference(large.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), base))));

        const QImage pickaxe = *QcProceduralTexture::render(procedural("pickaxe", "#c0c0c0", "#6b4a2b"), 64);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const int alpha = pickaxe.pixelColor(x, y).alpha();
                QVERIFY2(alpha == 0 || alpha == 255, qPrintable(u"%1,%2: %3"_s.arg(x).arg(y).arg(alpha)));
            }
        }
        // Smoothing changes diagonal outlines (a stick) instead of only doubling pixels, and a bevel
        // lights the bricks below the mortar.
        QJsonObject stick = procedural("stick", "#6b4a2b");
        stick.insert(u"detail"_s, QJsonObject{{u"grain"_s, 0.0}, {u"bevel"_s, 0.0}, {u"smooth"_s, true}});
        const QImage smoothStick = *QcProceduralTexture::render(stick, 32);
        QVERIFY(smoothStick != QcProceduralTexture::render(stick, 16)->scaled(32, 32));
        QJsonObject flatBricks = procedural("bricks", "#a04030", "#302020");
        flatBricks.insert(u"variation"_s, 0.0);
        flatBricks.insert(u"detail"_s, QJsonObject{{u"grain"_s, 0.0}, {u"bevel"_s, 0.3}});
        const QImage bevelled = *QcProceduralTexture::render(flatBricks, 64);
        // Rows 0-2 of the 16 x 16 bricks are brick, row 3 (and 15 above, the tiles repeat) dark mortar:
        // at 64, y 0 is lit, y 6 plain.
        QVERIFY(bevelled.pixelColor(20, 0).red() > bevelled.pixelColor(20, 6).red());

        // A known shape with an exact result: a diagonal line on a transparent background. Scale2x
        // treats transparent pixels as one area and opaque ones as another, so both sides of the
        // line get one quarter pixel each (the corners towards the line): 16 x 4 pixels of the
        // line, 15 on each side and one more on each side where the line wraps around the tile.
        QImage diagonal(16, 16, QImage::Format_RGBA8888);
        diagonal.fill(Qt::transparent);
        for (int i = 0; i < 16; ++i) {
            diagonal.setPixelColor(i, i, Qt::white);
        }
        const QImage enlarged = QcTextureDetail::enlarge(diagonal, 32, {0.0, 0.0, true}, 1);
        int opaque = 0;
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                opaque += enlarged.pixelColor(x, y).alpha() > 0;
            }
        }
        QCOMPARE(opaque, 96);
        // Next to the line pixel (3, 3): the lower left quarter of (4, 3) and the upper right
        // quarter of (3, 4) are filled, the other quarters of (4, 3) stay empty.
        QCOMPARE(enlarged.pixelColor(8, 7).alpha(), 255);
        QCOMPARE(enlarged.pixelColor(7, 8).alpha(), 255);
        QCOMPARE(enlarged.pixelColor(9, 7).alpha(), 0);
        QCOMPARE(enlarged.pixelColor(8, 6).alpha(), 0);

        // Glass keeps its partial transparency.
        const QImage glass = *QcProceduralTexture::render(procedural("glass", "#c0e0f0", "#e0f0ff"), 32);
        QVERIFY(glass.pixelColor(16, 20).alpha() > 0 && glass.pixelColor(16, 20).alpha() < 128);
    }

    void detailParametersComeFromThePatternAndTheDefinition()
    {
        using QcTextureDetail::forPattern;
        QVERIFY(!forPattern(QcTextureSpec::Pattern::Bricks).smooth);
        QVERIFY(forPattern(QcTextureSpec::Pattern::Pickaxe).smooth);
        QCOMPARE(forPattern(QcTextureSpec::Pattern::Glass).grain, 0.0);
        // Ice tiles without seams (decision E-860): a bevel would draw the tile's edge.
        QCOMPARE(forPattern(QcTextureSpec::Pattern::Ice).bevel, 0.0);
        QCOMPARE(forPattern(QcTextureSpec::Pattern::PackedIce).bevel, 0.0);
        QVERIFY(forPattern(QcTextureSpec::Pattern::Speckle).grain > forPattern(QcTextureSpec::Pattern::Pickaxe).grain);

        const QcTextureDetail::Params changed =
            QcTextureDetail::withOverrides(forPattern(QcTextureSpec::Pattern::Bricks), {{u"grain"_s, 0.5}, {u"smooth"_s, true}, {u"bevel"_s, 3.0}});
        QCOMPARE(changed.grain, 0.5);
        QVERIFY(changed.smooth);
        QCOMPARE(changed.bevel, 1.0);

        // A flat colour without grain and bevel only gets larger.
        QJsonObject flat = ::flat("#406080");
        QCOMPARE(*QcProceduralTexture::render(flat, 64), QcProceduralTexture::render(flat, 16)->scaled(64, 64));
        flat.insert(u"detail"_s, QJsonObject{{u"grain"_s, 0.2}});
        QVERIFY(*QcProceduralTexture::render(flat, 64) != QcProceduralTexture::render(flat, 16)->scaled(64, 64));
    }

    // Switching packs at run time (decision E-301), as the settings do it: the second switch reuses
    // the pack loaded by the first, the new atlas has other pixels but the same UVs, so the world
    // keeps its meshes.
    void switchingPacksKeepsTheLayoutAndReusesLoadedPacks()
    {
        QTemporaryDir user;
        QVERIFY(writePack(user.filePath(u"warm"_s), 32, u"red"_s, Qt::yellow));
        QVERIFY(writePack(user.filePath(u"cold"_s), 64, u"red"_s, Qt::cyan));
        const QList<QcTexturePackLibrary::Entry> entries =
            QcTexturePackLibrary::scan({{user.path(), QcTexturePack::Origin::User, u"user:"_s}});
        const auto content = std::make_shared<const QcContentRegistry>(smallContent());

        const QcTextureStackBuild::Result first = QcTextureStackBuild::run(stackRequest({u"user:warm"_s}, entries, {}, content));
        QCOMPARE(first.stack.size(), 1);
        QCOMPARE(first.atlas->tileSize(), 32);
        QCOMPARE(centre(*first.atlas, u"t:red"_s), QColor(Qt::yellow));

        QStringList ids = QcTexturePackLibrary::withActive({u"user:warm"_s}, u"user:cold"_s, true);
        QCOMPARE(ids, (QStringList{u"user:cold"_s, u"user:warm"_s}));
        // The warm pack is already loaded: it is not read again (its folder is gone now).
        QVERIFY(QDir(user.filePath(u"warm"_s)).removeRecursively());
        const QcTextureStackBuild::Result second =
            QcTextureStackBuild::run(stackRequest(ids, entries, {{u"user:warm"_s, first.stack.front()}}, content));
        QCOMPARE(second.stack.size(), 2);
        QVERIFY(second.missing.isEmpty());
        QCOMPARE(second.stack.back(), first.stack.front());
        QCOMPARE(second.atlas->tileSize(), 64);
        QCOMPARE(centre(*second.atlas, u"t:red"_s), QColor(Qt::cyan));
        QVERIFY(second.atlas->sameLayout(*first.atlas));

        // Moving the warm pack up makes it win again; a pack that is gone is reported.
        ids = QcTexturePackLibrary::withMoved(ids, u"user:warm"_s, -1);
        QCOMPARE(ids, (QStringList{u"user:warm"_s, u"user:cold"_s}));
        ids.append(u"user:gone"_s);
        const QcTextureStackBuild::Result third =
            QcTextureStackBuild::run(stackRequest(ids, entries, {{u"user:warm"_s, first.stack.front()}}, content));
        QCOMPARE(centre(*third.atlas, u"t:red"_s), QColor(Qt::yellow));
        QCOMPARE(third.missing, QStringList{u"user:gone"_s});
        QCOMPARE(QcTexturePackLibrary::withActive(ids, u"user:cold"_s, false), (QStringList{u"user:warm"_s, u"user:gone"_s}));
        QCOMPARE(QcTexturePackLibrary::withMoved(ids, u"user:none"_s, 1), ids);
        QCOMPARE(QcTexturePackLibrary::withMoved(ids, u"user:gone"_s, 5), ids);

        // The active packs together must fit the memory budget: the lowest priority goes first.
        QcTextureStackBuild::Request tight = stackRequest({u"user:cold"_s, u"user:warm"_s}, entries, {{u"user:warm"_s, first.stack.front()}}, content);
        tight.memoryBudget = qint64(64) * 64 * 4;
        const QcTextureStackBuild::Result budget = QcTextureStackBuild::run(tight);
        QCOMPARE(budget.stack.size(), 1);
        QCOMPARE(budget.stack.front()->id, u"user:cold"_s);
        QCOMPARE(budget.overBudget, QStringList{u"user:warm"_s});

        // The folders can be listed again in the same run; previews are read once per file.
        QcTextureStackBuild::Request rescan = stackRequest({u"user:cold"_s}, {}, {}, nullptr);
        rescan.rescan = QList<QcTexturePackLibrary::Root>{{user.path(), QcTexturePack::Origin::User, u"user:"_s}};
        rescan.generation = 7;
        const QcTextureStackBuild::Result listed = QcTextureStackBuild::run(rescan);
        QCOMPARE(listed.generation, quint64(7));
        QCOMPARE(listed.entries.size(), 1);
        QCOMPARE(listed.stack.size(), 1);

        // Without content (in the menu) only the packs are loaded.
        QVERIFY(!QcTextureStackBuild::run(stackRequest(ids, entries, {}, nullptr)).atlas);
    }

    // The settings build atlases in worker threads; several builds sharing packs and content must
    // not disturb each other (run under TSan).
    void buildsInWorkerThreadsShareTheirInput()
    {
        const auto content = std::make_shared<const QcContentRegistry>(smallContent());
        const QcTexturePackRef hd = pack(u"hd"_s, 64, {{u"t:green"_s, filled(64, Qt::white)}}, {}, true);
        const QcTextureStackBuild::Request request = stackRequest({u"hd"_s}, {}, {{u"hd"_s, hd}}, content);
        QList<QFuture<QcTextureStackBuild::Result>> builds;
        for (int i = 0; i < 4; ++i) {
            builds.append(QtConcurrent::run(&QcTextureStackBuild::run, request));
        }
        const QImage expected = QcTextureStackBuild::run(request).atlas->image();
        for (QFuture<QcTextureStackBuild::Result> &build : builds) {
            QCOMPARE(build.result().atlas->image(), expected);
        }
    }

    void builtInPacksLoadCleanly()
    {
        const QList<QcTexturePackLibrary::Entry> entries = QcTexturePackLibrary::scan(QcTexturePackLibrary::roots({}));
        QStringList ids;
        for (const QcTexturePackLibrary::Entry &entry : entries) {
            if (entry.origin == QcTexturePack::Origin::BuiltIn) {
                ids.append(entry.id);
            }
        }
        QVERIFY(ids.contains(u"builtin:qtcraft_hd_32"_s));
        QVERIFY(ids.contains(u"builtin:qtcraft_hd_64"_s));
        QVERIFY(ids.contains(u"builtin:qtcraft_hd_128"_s));
        QVERIFY(ids.contains(u"builtin:storybook_32"_s));

        const QcTexturePackLibrary::LoadedStack loaded = QcTexturePackLibrary::loadStack(ids, entries);
        QVERIFY(loaded.missing.isEmpty());
        const QcTexturePackStack &stack = loaded.stack;
        for (const QcTexturePackRef &loaded : stack) {
            QStringList problems;
            for (const QcTexturePackProblem &problem : loaded->problems) {
                problems.append(problem.englishText());
            }
            QVERIFY2(problems.isEmpty(), qPrintable(loaded->id + u": "_s + problems.join(u"; "_s)));
            QVERIFY(!loaded->preview.isNull());
            QVERIFY(!loaded->manifest.license.isEmpty());
        }

        // The image example pack: plain 32 x 32 PNG files of the base game's textures, no specs.
        const auto storybook = std::ranges::find(stack, u"builtin:storybook_32"_s, &QcTexturePack::id);
        QVERIFY(storybook != stack.end());
        QVERIFY(!(*storybook)->manifest.procedural);
        QVERIFY((*storybook)->images.size() >= 50);
        QVERIFY((*storybook)->specs.isEmpty());
        QCOMPARE((*storybook)->images.value(u"qtcraft:stone"_s).size(), QSize(32, 32));

        const auto content = QcTestUtil::baseContent();
        const QcTextureAtlas atlas = QcTextureAtlas::build(*content->registry, {*storybook});
        QCOMPARE(atlas.tileSize(), 32);
        QCOMPARE(atlas.sources().value(u"builtin:storybook_32"_s), (*storybook)->images.size());
    }

    // Experience orbs ask the atlas for a texture it has, not for the missing tile (decision E-399).
    void experienceOrbsUseTheirOwnTile()
    {
        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        const QcTextureAtlas atlas = QcTextureAtlas::build(*content->registry);
        QStringList asked;
        const QcEntityMesher::UvLookup uv = [&](const QString &texture) {
            asked.append(texture);
            return atlas.uv(texture);
        };
        QcClientEntity orb;
        orb.kind = QcEntityKind::ExperienceOrb;
        orb.health = 7.0;
        QcEntityMesh mesh;
        QcEntityMesher::appendOrb(mesh, orb, {1.0, 0.0, 1.0f}, uv);

        QVERIFY(!mesh.vertices.isEmpty());
        QVERIFY(!asked.isEmpty());
        const QRectF missing = atlas.uv(QString::fromLatin1(QcTextureAtlas::missingTextureName));
        for (const QString &texture : std::as_const(asked)) {
            QVERIFY2(atlas.uv(texture) != missing, qPrintable(texture));
        }
    }

private:
    QTemporaryDir m_data;
};

QTEST_GUILESS_MAIN(TestTextureAtlas)
#include "test_texture_atlas.moc"
