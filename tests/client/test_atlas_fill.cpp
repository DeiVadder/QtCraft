// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/icons/QcBlockIcon.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/meshing/QcFigureLook.h"
#include "clientcore/texturepacks/QcTexturePack.h"
#include "clientcore/textures/QcAlphaCoverage.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcContentRegistry.h"
#include "QcTestUtil.h"

#include <QBuffer>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <utility>

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

// A procedural texture of one flat colour.
QJsonObject flat(const char *primary)
{
    return {{u"pattern"_s, u"noise"_s},
            {u"primary"_s, QString::fromLatin1(primary)},
            {u"secondary"_s, u"#202020"_s},
            {u"seed"_s, 7},
            {u"variation"_s, 0.0},
            {u"detail"_s, QJsonObject{{u"grain"_s, 0.0}, {u"bevel"_s, 0.0}}}};
}

// A pack made in memory with images by texture name.
QcTexturePackRef pack(const QString &id, int resolution, const QHash<QString, QImage> &images)
{
    auto made = std::make_shared<QcTexturePack>();
    made->id = id;
    made->manifest.name = id;
    made->manifest.resolution = resolution;
    made->images = images;
    return made;
}

// A 16 x 16 cut-out picture: a cross of opaque texels in two colours, transparent black around it.
QImage cutOutTile()
{
    QImage tile(16, 16, QImage::Format_RGBA8888);
    tile.fill(QColor(0, 0, 0, 0));
    for (int i = 2; i < 14; ++i) {
        tile.setPixelColor(i, 7, QColor(40, 160, 30, 255));
        tile.setPixelColor(7, i, QColor(220, 200, 20, 255));
    }
    return tile;
}

} // namespace

// The colour of fully transparent atlas texels (decision E-332): it may not show anywhere, in the
// tiles, the cut-out coverage of the mip levels or the item icons, and it must not darken the edges
// of plants where the GPU blends two mip levels.
class TestAtlasFill : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Never the player's own data folder.
        QVERIFY(m_data.isValid());
        qputenv("QTCRAFT_DATA_DIR", m_data.path().toUtf8());
    }

    // Transparent texels take the colour of the nearest drawn one (decision E-332), so blending
    // two mip levels on the GPU does not darken the edges of plants; alpha stays 0.
    void transparentTexelsTakeTheNearestColour()
    {
        QImage tile(4, 1, QImage::Format_RGBA8888);
        tile.fill(QColor(0, 0, 0, 0));
        tile.setPixelColor(0, 0, QColor(10, 200, 30, 255));
        tile.setPixelColor(3, 0, QColor(250, 250, 0, 128));
        QcAlphaCoverage::fillTransparentColour(tile);
        QCOMPARE(tile.pixelColor(1, 0), QColor(10, 200, 30, 0));
        QCOMPARE(tile.pixelColor(2, 0), QColor(250, 250, 0, 0));
        QCOMPARE(tile.pixelColor(0, 0), QColor(10, 200, 30, 255));
        QCOMPARE(tile.pixelColor(3, 0), QColor(250, 250, 0, 128));

        // Further away the colour spreads ring by ring; a tile with nothing drawn stays as it is.
        QImage wide(5, 5, QImage::Format_RGBA8888);
        wide.fill(QColor(0, 0, 0, 0));
        wide.setPixelColor(2, 2, QColor(1, 2, 3, 255));
        QcAlphaCoverage::fillTransparentColour(wide);
        QCOMPARE(wide.pixelColor(0, 0), QColor(1, 2, 3, 0));
        QCOMPARE(wide.pixelColor(4, 4), QColor(1, 2, 3, 0));
        QImage empty(3, 3, QImage::Format_RGBA8888);
        empty.fill(QColor(9, 9, 9, 0));
        QcAlphaCoverage::fillTransparentColour(empty);
        QCOMPARE(empty.pixelColor(1, 1), QColor(9, 9, 9, 0));

        // A fully transparent block keeps the mean of its colours in the next level.
        QImage level(2, 2, QImage::Format_RGBA8888);
        level.fill(QColor(100, 0, 40, 0));
        level.setPixelColor(1, 1, QColor(0, 0, 0, 0));
        QCOMPARE(QcAtlasLayout::halve(level).pixelColor(0, 0), QColor(75, 0, 30, 0));
    }

    // At equal distance the texel above or to the left gives its colour: the same image always
    // gives the same result.
    void fillTiesGoToTheTexelAboveOrLeft()
    {
        QImage row(3, 1, QImage::Format_RGBA8888);
        row.fill(QColor(0, 0, 0, 0));
        row.setPixelColor(0, 0, QColor(255, 0, 0, 255));
        row.setPixelColor(2, 0, QColor(0, 0, 255, 255));
        QcAlphaCoverage::fillTransparentColour(row);
        QCOMPARE(row.pixelColor(1, 0), QColor(255, 0, 0, 0));

        QImage column(1, 3, QImage::Format_RGBA8888);
        column.fill(QColor(0, 0, 0, 0));
        column.setPixelColor(0, 2, QColor(0, 0, 255, 255));
        column.setPixelColor(0, 0, QColor(255, 0, 0, 255));
        QcAlphaCoverage::fillTransparentColour(column);
        QCOMPARE(column.pixelColor(0, 1), QColor(255, 0, 0, 0));

        QImage first = cutOutTile();
        QImage second = cutOutTile();
        QcAlphaCoverage::fillTransparentColour(first);
        QcAlphaCoverage::fillTransparentColour(second);
        QCOMPARE(first, second);
    }

    // The fill changes only the colour of texels with alpha 0: alpha and so the cut-out coverage
    // of every level stay, and every drawn texel keeps its colour.
    void fillKeepsAlphaCoverageAndDrawnTexels()
    {
        const QImage before = cutOutTile();
        QImage after = before;
        QcAlphaCoverage::fillTransparentColour(after);
        QVERIFY(after != before);
        for (int y = 0; y < before.height(); ++y) {
            for (int x = 0; x < before.width(); ++x) {
                QCOMPARE(after.pixelColor(x, y).alpha(), before.pixelColor(x, y).alpha());
                if (before.pixelColor(x, y).alpha() > 0) {
                    QCOMPARE(after.pixel(x, y), before.pixel(x, y));
                }
            }
        }

        const QcAtlasLayout layout(1);
        QImage plain(layout.extent(16), QImage::Format_RGBA8888);
        plain.fill(Qt::transparent);
        QImage filledLevel = plain;
        layout.place(plain, 0, before);
        layout.place(filledLevel, 0, after);
        QCOMPARE(QcAlphaCoverage::measure(filledLevel, layout, 1, 16), QcAlphaCoverage::measure(plain, layout, 1, 16));
        const QList<double> coverage = QcAlphaCoverage::measure(plain, layout, 1, 16);
        QVERIFY(coverage.constFirst() > 0.0);
    }

    // Image textures with alpha 0, 128 and 255, from a 16x image pack and from a content PNG: in
    // the atlas every texel with alpha above 0 is bit for bit the one of the image.
    void imageTexturesKeepTheirDrawnTexels()
    {
        QImage picture = cutOutTile();
        picture.setPixelColor(5, 5, QColor(10, 20, 30, 128));
        QcContentRegistry content;
        content.assets().registerTexture({u"t:red"_s, flat("#c00000"), {}});
        content.assets().registerTexture({u"t:picture"_s, {}, png(picture)});
        QImage packed = cutOutTile();
        packed.setPixelColor(3, 3, QColor(200, 100, 50, 128));
        const QcTexturePackRef images = pack(u"images"_s, 16, {{u"t:red"_s, packed}});
        const QcTextureAtlas atlas = QcTextureAtlas::build(content, {images});
        QCOMPARE(atlas.tileSize(), 16);
        for (const auto &[name, source] : {std::pair(u"t:picture"_s, picture), std::pair(u"t:red"_s, packed)}) {
            const QImage tile = atlas.tile(name);
            int drawn = 0;
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    QCOMPARE(tile.pixelColor(x, y).alpha(), source.pixelColor(x, y).alpha());
                    if (source.pixelColor(x, y).alpha() > 0) {
                        QCOMPARE(tile.pixel(x, y), source.pixel(x, y));
                        ++drawn;
                    }
                }
            }
            QVERIFY2(drawn > 0 && drawn < 256, qPrintable(name));
        }
    }

    // Inventory icons do not change with the colour of transparent texels: a block icon drawn from
    // the filled atlas equals one drawn from the same atlas with those colours back at black, and a
    // flat icon stays transparent around its picture. (The character previews draw entities with
    // alpha 1 and leave their background alone; checked on 28-inventory-*.png, decision E-332.)
    void iconsDoNotShowTheTransparentColours()
    {
        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        const QcTextureAtlas atlas = QcTextureAtlas::build(*content->registry);
        const QImage &level = atlas.levelForTile(QcBlockIcon::sourceTileSize);
        QImage black = level;
        for (int y = 0; y < black.height(); ++y) {
            uchar *line = black.scanLine(y);
            for (int x = 0; x < black.width(); ++x) {
                if (line[static_cast<std::ptrdiff_t>(x) * 4 + 3] == 0) {
                    std::fill_n(line + static_cast<std::ptrdiff_t>(x) * 4, 3, static_cast<uchar>(0));
                }
            }
        }
        QVERIFY(black != level);

        const QcBlockRenderTable table(*content->registry, atlas);
        const std::optional<QcBlockId> leaves = content->registry->blockId(u"qtcraft:oak_leaves"_s);
        QVERIFY(leaves.has_value());
        const QcBlockRenderTable::Entry &entry = table.entry(QcFigureLook::itemState(*content->registry, *leaves));
        const QImage filledIcon = QcBlockIcon::render(entry, QcBlockIcon::shadedAtlases(level));
        const QImage blackIcon = QcBlockIcon::render(entry, QcBlockIcon::shadedAtlases(black));
        QCOMPARE(filledIcon.convertToFormat(QImage::Format_ARGB32_Premultiplied), blackIcon.convertToFormat(QImage::Format_ARGB32_Premultiplied));

        QVERIFY(atlas.uv(u"qtcraft:poppy"_s) != atlas.uv(u"t:unknown"_s));
        const QImage poppy = atlas.tile(u"qtcraft:poppy"_s, QcBlockIcon::largeIconSize);
        QVERIFY(!poppy.isNull());
        QCOMPARE(poppy.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(poppy.pixelColor(poppy.width() - 1, 0).alpha(), 0);
    }

    // In the atlas no transparent texel of a plant is black, in any mip level: the cut-out tiles
    // carry their colour into their transparent texels and the smaller levels.
    void plantTilesHaveNoBlackTransparentTexels()
    {
        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        const QcTextureAtlas atlas = QcTextureAtlas::build(*content->registry, {});
        for (const QString &name : {u"qtcraft:tall_grass"_s, u"qtcraft:poppy"_s, u"qtcraft:oak_leaves"_s, u"qtcraft:dandelion"_s}) {
            const QRectF uv = atlas.uv(name);
            QVERIFY2(uv != atlas.uv(u"t:unknown"_s), qPrintable(name));
            for (const QImage &level : atlas.levels()) {
                const QRect tile(static_cast<int>(std::lround(uv.x() * level.width())), static_cast<int>(std::lround(uv.y() * level.height())),
                                 static_cast<int>(std::lround(uv.width() * level.width())), static_cast<int>(std::lround(uv.height() * level.height())));
                int transparent = 0;
                int black = 0;
                for (int y = tile.top(); y <= tile.bottom(); ++y) {
                    for (int x = tile.left(); x <= tile.right(); ++x) {
                        const QColor texel = level.pixelColor(x, y);
                        transparent += texel.alpha() == 0;
                        black += texel.alpha() == 0 && texel.red() + texel.green() + texel.blue() == 0;
                    }
                }
                QVERIFY2(&level != &atlas.levels().front() || transparent > 0, qPrintable(name));
                QCOMPARE(black, 0);
            }
        }
    }

private:
    QTemporaryDir m_data;
};

QTEST_MAIN(TestAtlasFill)
#include "test_atlas_fill.moc"
