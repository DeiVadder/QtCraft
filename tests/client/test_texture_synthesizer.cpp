// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcProceduralTexture.h"
#include "clientcore/textures/QcTextureSynthesizer.h"

#include <QJsonObject>
#include <QTest>

#include <array>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace {

int transparentPixels(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            count += image.pixelColor(x, y).alpha() == 0;
        }
    }
    return count;
}

int difference(const QImage &image, QPoint first, QPoint second)
{
    const QColor a = image.pixelColor(first);
    const QColor b = image.pixelColor(second);
    return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue()) + std::abs(a.alpha() - b.alpha());
}

// How much neighbouring pixels differ across the tile's edge (the last column against the first, the
// last row against the first: neighbours when blocks lie side by side) and across the lines inside it
// that divide the tile into quarters (where the patterns' coarse structure changes too).
struct Seams
{
    double edge = 0.0;
    double inside = 0.0;
};

double lineDifference(const QImage &image, int before, int after)
{
    double sum = 0.0;
    for (int a = 0; a < image.width(); ++a) {
        sum += difference(image, {before, a}, {after, a}) + difference(image, {a, before}, {a, after});
    }
    return sum / (2.0 * image.width());
}

Seams seamsOf(const QImage &image)
{
    const int size = image.width();
    const int quarter = size / 4;
    const double inside = (lineDifference(image, quarter - 1, quarter) + lineDifference(image, 2 * quarter - 1, 2 * quarter)
                           + lineDifference(image, 3 * quarter - 1, 3 * quarter)) / 3.0;
    return {lineDifference(image, size - 1, 0), inside};
}

// Summed over the channels: how far the mean colour of the outermost ring of pixels lies from the
// mean colour inside it (a frame is a ring of another colour).
double ringContrast(const QImage &image)
{
    std::array<double, 4> ring{};
    std::array<double, 4> inner{};
    int ringCount = 0;
    int innerCount = 0;
    const int last = image.width() - 1;
    for (int y = 0; y <= last; ++y) {
        for (int x = 0; x <= last; ++x) {
            const QColor c = image.pixelColor(x, y);
            const std::array<double, 4> channels = {static_cast<double>(c.red()), static_cast<double>(c.green()),
                static_cast<double>(c.blue()), static_cast<double>(c.alpha())};
            const bool onRing = x == 0 || y == 0 || x == last || y == last;
            std::array<double, 4> &sum = onRing ? ring : inner;
            for (std::size_t i = 0; i < 4; ++i) {
                sum[i] += channels[i];
            }
            (onRing ? ringCount : innerCount) += 1;
        }
    }
    double contrast = 0.0;
    for (std::size_t i = 0; i < 4; ++i) {
        contrast += std::abs(ring[i] / ringCount - inner[i] / innerCount);
    }
    return contrast;
}

// No frame and no seam: the ring at the edge has the colour of the inside, and neighbouring blocks
// continue each other about as well as the quarters of one tile do.
bool tilesWithoutFrame(const QImage &image)
{
    const Seams seams = seamsOf(image);
    const double ring = ringContrast(image);
    qInfo("edge %.1f, quarter lines %.1f, ring contrast %.1f", seams.edge, seams.inside, ring);
    return seams.edge <= 1.5 * seams.inside + 4.0 && ring <= 20.0;
}

QJsonObject definition(const QString &pattern, const QString &primary, const QString &secondary)
{
    return {{u"pattern"_s, pattern}, {u"primary"_s, primary}, {u"secondary"_s, secondary}, {u"variation"_s, 0.05}, {u"seed"_s, 13}};
}

int alphaBelow(const QImage &image, int limit)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            count += image.pixelColor(x, y).alpha() < limit;
        }
    }
    return count;
}

} // namespace

class TestTextureSynthesizer : public QObject
{
    Q_OBJECT

private slots:
    void sameSpecProducesIdenticalImage()
    {
        const QcTextureSpec spec{QcTextureSpec::Pattern::Ore, QColor(120, 120, 120), QColor(200, 150, 40), 0.1, 42};
        QCOMPARE(QcTextureSynthesizer::synthesize(spec), QcTextureSynthesizer::synthesize(spec));
    }

    void differentSeedChangesNoise()
    {
        QcTextureSpec spec{QcTextureSpec::Pattern::Noise, QColor(120, 120, 120), QColor(), 0.2, 1};
        const QImage first = QcTextureSynthesizer::synthesize(spec);
        spec.seed = 2;
        QVERIFY(first != QcTextureSynthesizer::synthesize(spec));
    }

    void tileHasFixedSize()
    {
        const QImage image = QcTextureSynthesizer::synthesize({});
        QCOMPARE(image.size(), QSize(16, 16));
    }

    void solidPatternsAreOpaqueAndLeavesHaveHoles()
    {
        const QcTextureSpec stone{QcTextureSpec::Pattern::Speckle, QColor(125, 125, 125), QColor(100, 100, 100), 0.1, 3};
        QCOMPARE(transparentPixels(QcTextureSynthesizer::synthesize(stone)), 0);

        const QcTextureSpec leaves{QcTextureSpec::Pattern::Leaves, QColor(60, 130, 50), QColor(40, 100, 40), 0.1, 3};
        const int holes = transparentPixels(QcTextureSynthesizer::synthesize(leaves));
        // Roughly one in six pixels is cut out; accept a wide band around 256 / 6.
        QVERIFY2(holes > 15 && holes < 90, qPrintable(QString::number(holes)));
    }

    void glassIsMostlyTranslucent()
    {
        const QcTextureSpec glass{QcTextureSpec::Pattern::Glass, QColor(200, 230, 240), QColor(220, 240, 250), 0.05, 1};
        const QImage image = QcTextureSynthesizer::synthesize(glass);
        QCOMPARE(image.pixelColor(0, 0).alpha(), 255);
        QVERIFY(image.pixelColor(12, 4).alpha() < 128);
    }

    // Ice and packed ice have no frame: neighbouring blocks continue each other at 16 and enlarged
    // (decision E-860); glass, with its frame, does not.
    void iceTilesWithoutAFrame_data()
    {
        QTest::addColumn<QString>("pattern");
        QTest::addColumn<QString>("primary");
        QTest::addColumn<QString>("secondary");
        QTest::addColumn<int>("resolution");
        QTest::addColumn<bool>("seamless");
        for (const int resolution : {16, 64}) {
            const QByteArray at = QByteArray::number(resolution);
            QTest::newRow(QByteArray("ice " + at).constData()) << u"ice"_s << u"#9ec4f290"_s << u"#e2effc"_s << resolution << true;
            QTest::newRow(QByteArray("blue ice " + at).constData()) << u"ice"_s << u"#5a92ea"_s << u"#8cbaf6"_s << resolution << true;
            QTest::newRow(QByteArray("packed ice " + at).constData()) << u"packed_ice"_s << u"#a4c2ee"_s << u"#cadcf6"_s << resolution << true;
            QTest::newRow(QByteArray("glass " + at).constData()) << u"glass"_s << u"#9ec4f2"_s << u"#b8d6f8"_s << resolution << false;
        }
    }

    void iceTilesWithoutAFrame()
    {
        QFETCH(QString, pattern);
        QFETCH(QString, primary);
        QFETCH(QString, secondary);
        QFETCH(int, resolution);
        QFETCH(bool, seamless);
        const QcResult<QImage> image = QcProceduralTexture::render(definition(pattern, primary, secondary), resolution);
        QVERIFY(image.has_value());
        QCOMPARE(tilesWithoutFrame(*image), seamless);
    }

    // Ice keeps the alpha of its primary colour (translucent ice, opaque blue ice); packed ice is opaque.
    void iceKeepsTheAlphaOfItsColour()
    {
        const QcTextureSpec ice{QcTextureSpec::Pattern::Ice, QColor(158, 196, 242, 0x90), QColor(226, 239, 252), 0.05, 13};
        const QImage translucent = QcTextureSynthesizer::synthesize(ice);
        QCOMPARE(alphaBelow(translucent, 255), 16 * 16);
        QCOMPARE(alphaBelow(translucent, 0x90), 0);

        QcTextureSpec blue = ice;
        blue.primary = QColor(90, 146, 234);
        QCOMPARE(alphaBelow(QcTextureSynthesizer::synthesize(blue), 255), 0);

        const QcTextureSpec packed{QcTextureSpec::Pattern::PackedIce, QColor(164, 194, 238, 0x90), QColor(202, 220, 246, 0x90), 0.04, 909};
        QCOMPARE(alphaBelow(QcTextureSynthesizer::synthesize(packed), 255), 0);
    }

    // Slime: a translucent fill in the primary colour inside a rim in the secondary one, both with
    // their own alpha (decision E-860).
    void slimeIsATranslucentFillInARim()
    {
        QCOMPARE(qcTexturePatternFromName(u"slime"_s), QcTextureSpec::Pattern::Slime);
        const QcTextureSpec slime{QcTextureSpec::Pattern::Slime, QColor(114, 208, 100, 0xb0), QColor(78, 168, 66, 0xd0), 0.05, 908};
        const QImage image = QcTextureSynthesizer::synthesize(slime);
        for (const QPoint rim : {QPoint(0, 0), QPoint(15, 7), QPoint(7, 15), QPoint(0, 9)}) {
            QCOMPARE(image.pixelColor(rim).alpha(), 0xd0);
        }
        for (const QPoint inside : {QPoint(1, 1), QPoint(7, 7), QPoint(14, 14)}) {
            QCOMPARE(image.pixelColor(inside).alpha(), 0xb0);
            QVERIFY(image.pixelColor(inside).green() > image.pixelColor(0, 0).green());
        }
    }

    void furnaceFrontHasADarkOpeningInAStoneFace()
    {
        QCOMPARE(qcTexturePatternFromName(u"furnace"_s), QcTextureSpec::Pattern::Furnace);
        const QcTextureSpec spec{QcTextureSpec::Pattern::Furnace, QColor(130, 130, 130), QColor(30, 26, 24), 0.0, 5};
        const QImage image = QcTextureSynthesizer::synthesize(spec);
        QCOMPARE(transparentPixels(image), 0);
        // Above the opening the stone colour, inside it the dark secondary colour.
        QCOMPARE(image.pixelColor(7, 3), QColor(130, 130, 130));
        QCOMPARE(image.pixelColor(7, 9), QColor(30, 26, 24));
    }

    // Arm textures read as round limbs: darker at the left and right edge than in the middle; the
    // sleeve ends in a darker cuff, the skin in finger gaps.
    void armTexturesAreShadedAndHaveCuffAndFingers()
    {
        QCOMPARE(qcTexturePatternFromName(u"limb"_s), QcTextureSpec::Pattern::Limb);
        QCOMPARE(qcTexturePatternFromName(u"sleeve"_s), QcTextureSpec::Pattern::Sleeve);
        const QcTextureSpec skinSpec{QcTextureSpec::Pattern::Limb, QColor(240, 240, 240), QColor(200, 200, 200), 0.0, 1};
        const QImage skin = QcTextureSynthesizer::synthesize(skinSpec);
        QCOMPARE(transparentPixels(skin), 0);
        QVERIFY(skin.pixelColor(0, 5).red() < skin.pixelColor(7, 5).red() - 20);
        QVERIFY(skin.pixelColor(15, 5).red() < skin.pixelColor(8, 5).red() - 20);
        QVERIFY(skin.pixelColor(4, 14).red() < skin.pixelColor(6, 14).red());

        const QcTextureSpec sleeveSpec{QcTextureSpec::Pattern::Sleeve, QColor(240, 240, 240), QColor(170, 170, 170), 0.0, 1};
        const QImage sleeve = QcTextureSynthesizer::synthesize(sleeveSpec);
        QCOMPARE(transparentPixels(sleeve), 0);
        QVERIFY(sleeve.pixelColor(7, 14).red() < sleeve.pixelColor(7, 4).red() - 30);
        QVERIFY(sleeve.pixelColor(0, 4).red() < sleeve.pixelColor(7, 4).red());
    }

    void patternNamesResolve()
    {
        QCOMPARE(qcTexturePatternFromName(u"planks"_s), QcTextureSpec::Pattern::Planks);
        QCOMPARE(qcTexturePatternFromName(u"log_top"_s), QcTextureSpec::Pattern::LogTop);
        QVERIFY(!qcTexturePatternFromName(u"no_such_pattern"_s).has_value());
    }
};

QTEST_GUILESS_MAIN(TestTextureSynthesizer)
#include "test_texture_synthesizer.moc"
