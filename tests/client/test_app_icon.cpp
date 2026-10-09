#include "QcAppIcon.h"
#include "QcBaseContent.h"
#include "QcTextureAtlas.h"

#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QImage solidTile(QColor colour)
{
    QImage tile(16, 16, QImage::Format_ARGB32);
    tile.fill(colour);
    return tile;
}

} // namespace

class TestAppIcon : public QObject
{
    Q_OBJECT

private slots:
    void iconSetHasTheTenImagesIconutilExpects()
    {
        const QList<QcAppIcon::IconFile> files = QcAppIcon::iconSetFiles();
        QCOMPARE(files.size(), 10);
        QStringList names;
        for (const QcAppIcon::IconFile &file : files) {
            names.append(u"%1:%2"_s.arg(file.name).arg(file.pixels));
        }
        const QStringList expected = {u"icon_16x16.png:16"_s,     u"icon_16x16@2x.png:32"_s,   u"icon_32x32.png:32"_s,
                                      u"icon_32x32@2x.png:64"_s,   u"icon_128x128.png:128"_s,   u"icon_128x128@2x.png:256"_s,
                                      u"icon_256x256.png:256"_s,   u"icon_256x256@2x.png:512"_s, u"icon_512x512.png:512"_s,
                                      u"icon_512x512@2x.png:1024"_s};
        QCOMPARE(names, expected);
    }

    void layoutOfTheIcon()
    {
        // Distinct flat colours show which texture lands where.
        const QColor top(0, 200, 0);
        const QColor side(200, 100, 0);
        const QColor tool(255, 0, 255);
        const QImage icon = QcAppIcon::render(solidTile(top), solidTile(side), solidTile(tool), 1024);
        QCOMPARE(icon.size(), QSize(1024, 1024));

        // Transparent outside the rounded tile (corners and margin), opaque inside.
        QCOMPARE(qAlpha(icon.pixel(0, 0)), 0);
        QCOMPARE(qAlpha(icon.pixel(130, 130)), 0);
        QCOMPARE(qAlpha(icon.pixel(1023, 512)), 0);
        QCOMPARE(qAlpha(icon.pixel(512, 120)), 255);

        // Top face at the block's upper middle: the top texture unshaded.
        QCOMPARE(QColor(icon.pixel(460, 380)), top);
        // Left side face below it: the side texture, darker than unshaded but the same hue.
        const QColor left = QColor(icon.pixel(330, 600));
        QVERIFY(left.red() < side.red() && left.red() > side.red() / 2);
        QCOMPARE(left.blue(), 0);

        // The right face is shaded more than the left one.
        const QColor right = QColor(icon.pixel(485, 700));
        QVERIFY2(right.red() < left.red(), qPrintable(right.name()));

        // The tool sits in the lower right quarter.
        QCOMPARE(QColor(icon.pixel(800, 800)), tool);
        // The sky behind is blue.
        const QColor sky = QColor(icon.pixel(850, 200));
        QVERIFY(sky.blue() > sky.red() && sky.blue() > sky.green());
    }

    void deterministicAndWrittenFromTheBaseGame()
    {
        QStringList warnings;
        const auto base = QcBaseContent::load(warnings);
        QVERIFY2(base, qPrintable(base ? QString() : base.error()));
        const QcTextureAtlas atlas = QcTextureAtlas::build(**base);
        const QImage first = QcAppIcon::render(atlas.tile(u"qtcraft:grass_top"_s), atlas.tile(u"qtcraft:grass_side"_s),
                                               atlas.tile(u"qtcraft:iron_pickaxe"_s), 256);
        const QImage second = QcAppIcon::render(atlas.tile(u"qtcraft:grass_top"_s), atlas.tile(u"qtcraft:grass_side"_s),
                                                atlas.tile(u"qtcraft:iron_pickaxe"_s), 256);
        QCOMPARE(first, second);

        // Faces show the real textures: a top face pixel is a texel of grass_top, a left face
        // pixel a texel of grass_side darkened like the world's side faces (alpha 50 of black).
        const QImage topTile = atlas.tile(u"qtcraft:grass_top"_s);
        const QImage sideTile = atlas.tile(u"qtcraft:grass_side"_s);
        const auto texelMatches = [](const QImage &tile, QColor colour, double shade) {
            for (int y = 0; y < tile.height(); ++y) {
                for (int x = 0; x < tile.width(); ++x) {
                    const QColor texel(tile.pixel(x, y));
                    if (std::abs(texel.red() * shade - colour.red()) <= 2.0 && std::abs(texel.green() * shade - colour.green()) <= 2.0
                        && std::abs(texel.blue() * shade - colour.blue()) <= 2.0) {
                        return true;
                    }
                }
            }
            return false;
        };

        QVERIFY(texelMatches(topTile, QColor(first.pixel(115, 95)), 1.0));
        QVERIFY(texelMatches(sideTile, QColor(first.pixel(82, 150)), 205.0 / 255.0));
        QVERIFY(!texelMatches(topTile, QColor(first.pixel(82, 150)), 1.0));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString iconSet = directory.filePath(u"QtCraft.iconset"_s);
        const QcStatus written = QcAppIcon::writeIconSet(atlas, iconSet);
        QVERIFY2(written, qPrintable(written ? QString() : written.error()));
        const QList<QcAppIcon::IconFile> files = QcAppIcon::iconSetFiles();
        for (const QcAppIcon::IconFile &file : files) {
            const QImage image(QDir(iconSet).filePath(file.name));
            QCOMPARE(image.size(), QSize(file.pixels, file.pixels));
        }

        // The 256 px file is the image rendered above.
        QCOMPARE(QImage(QDir(iconSet).filePath(u"icon_256x256.png"_s)).convertToFormat(first.format()), first);
    }

    void missingTextureIsAnError()
    {
        QcContentRegistry empty;
        const QcTextureAtlas atlas = QcTextureAtlas::build(empty);
        QTemporaryDir directory;
        const QcStatus written = QcAppIcon::writeIconSet(atlas, directory.filePath(u"icons"_s));
        QVERIFY(!written);
        QVERIFY(written.error().contains(u"qtcraft:grass_top"_s));
        QVERIFY(!QDir(directory.filePath(u"icons"_s)).exists());
    }
};

QTEST_GUILESS_MAIN(TestAppIcon)
#include "test_app_icon.moc"
