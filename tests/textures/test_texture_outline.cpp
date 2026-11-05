// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Outlines of enlarged cut-out textures (decision E-1000): the pixel variation never moves an
// outline, tools and weapons keep straight edges at every resolution, a handle meets its head,
// an icon's parts do not join across its border, and the enlarged sword and axe gain parts their
// 16 x 16 icons do not have.

#include "clientcore/textures/QcProceduralTexture.h"
#include "clientcore/textures/QcTextureDetail.h"
#include "clientcore/textures/QcTextureOutline.h"
#include "clientcore/textures/QcTextureSpec.h"

#include <QImage>
#include <QJsonObject>
#include <QPoint>
#include <QTest>

#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

QJsonObject procedural(const char *pattern, const char *primary, double variation)
{
    return {{u"pattern"_s, QString::fromLatin1(pattern)},
            {u"primary"_s, QString::fromLatin1(primary)},
            {u"secondary"_s, u"#6e5432"_s},
            {u"variation"_s, variation},
            {u"seed"_s, 319}};
}

QImage rendered(const QJsonObject &definition, int resolution)
{
    const QcResult<QImage> image = QcProceduralTexture::render(definition, resolution);
    return image ? *image : QImage();
}

// The visible pixels of `image` as 0 and 1, row by row.
QByteArray coverage(const QImage &image)
{
    QByteArray mask;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            mask.append(image.pixelColor(x, y).alpha() > 0 ? '1' : '0');
        }
    }
    return mask;
}

// A blade of two diagonals from the bottom left to the top right, its pixels coloured by `colour`.
template<typename Colour>
QImage blade(Colour colour)
{
    QImage image(16, 16, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    for (int x = 2; x <= 13; ++x) {
        image.setPixelColor(x, 15 - x, colour(x, 15 - x));
        image.setPixelColor(x, 16 - x, colour(x, 16 - x));
    }
    return image;
}

// Whether every pixel of the square of `size` pixels from `corner` of `image` is transparent.
bool clearSquare(const QImage &image, QPoint corner, int size)
{
    for (int y = corner.y(); y < corner.y() + size; ++y) {
        for (int x = corner.x(); x < corner.x() + size; ++x) {
            if (image.pixelColor(x, y).alpha() > 0) {
                return false;
            }
        }
    }
    return true;
}

// The first visible column of row `y`, if any.
std::optional<int> firstVisible(const QImage &image, int y)
{
    for (int x = 0; x < image.width(); ++x) {
        if (image.pixelColor(x, y).alpha() > 0) {
            return x;
        }
    }
    return std::nullopt;
}

// The number of visible pixels of row `y` from the first one up to the first transparent pixel
// after it; a row without a notch has all its visible pixels in that run.
int firstRun(const QImage &image, int y)
{
    int x = firstVisible(image, y).value_or(image.width());
    int run = 0;
    for (; x < image.width() && image.pixelColor(x, y).alpha() > 0; ++x) {
        ++run;
    }
    return run;
}

int visibleInRow(const QImage &image, int y)
{
    int visible = 0;
    for (int x = 0; x < image.width(); ++x) {
        visible += image.pixelColor(x, y).alpha() > 0 ? 1 : 0;
    }
    return visible;
}

} // namespace

class TestTextureOutline : public QObject
{
    Q_OBJECT

private slots:
    // The same shape in one colour and in strongly varied colours has the same outline with
    // Scale2x (plants, the bow): the variation used to notch and pinch thin blades.
    void theVariationDoesNotMoveTheOutline()
    {
        const QImage even = blade([](int, int) { return QColor(216, 216, 216); });
        const QImage varied = blade([](int x, int y) { return (x * 7 + y * 3) % 3 == 0 ? QColor(170, 170, 170) : QColor(250, 250, 250); });
        const QcTextureDetail::Params scale2x{.grain = 0.0, .bevel = 0.0, .smooth = true, .rimOnly = true, .strokes = false};
        for (const int resolution : {32, 64, 128}) {
            QCOMPARE(coverage(QcTextureDetail::enlarge(varied, resolution, scale2x, 1)), coverage(QcTextureDetail::enlarge(even, resolution, scale2x, 1)));
        }
    }

    // With strokes a blade of alternating colours is a straight band without notches: along its
    // middle each row starts one column left of the row above and is one run of the same length.
    // Scale2x left steps there, and its colour test used to cut notches between the colours.
    void aVariedBladeHasNoNotches()
    {
        const QImage varied = blade([](int x, int) { return x % 2 == 0 ? QColor(150, 150, 150) : QColor(250, 250, 250); });
        const QcTextureDetail::Params strokes{.grain = 0.0, .bevel = 0.0, .smooth = true, .rimOnly = true, .strokes = true};
        const int resolution = 128;
        const int scale = resolution / 16;
        const QImage image = QcTextureDetail::enlarge(varied, resolution, strokes, 1);
        for (int y = 5 * scale; y < 10 * scale; ++y) {
            const std::optional<int> upper = firstVisible(image, y);
            const std::optional<int> lower = firstVisible(image, y + 1);
            QVERIFY(upper && lower);
            QVERIFY2(*upper - *lower == 1, qPrintable(u"rows %1 and %2 start at %3 and %4"_s.arg(y).arg(y + 1).arg(*upper).arg(*lower)));
            QCOMPARE(firstRun(image, y), visibleInRow(image, y));
            QCOMPARE(firstRun(image, y), firstRun(image, y + 1));
        }
    }

    // An icon does not repeat: the plain tool's handle ends in the bottom left pixel and its head
    // in the top right one, and the clear corners between them stay clear (strokes across the
    // border used to draw triangles there).
    void partsDoNotJoinAcrossTheBorder()
    {
        const QJsonObject tool = procedural("tool", "#d8d8d8", 0.08);
        const QImage small = rendered(tool, 16);
        QVERIFY(small.pixelColor(0, 15).alpha() > 0);
        QVERIFY(small.pixelColor(15, 0).alpha() > 0);
        for (const int resolution : {32, 64, 128}) {
            const QImage image = rendered(tool, resolution);
            const int scale = resolution / 16;
            QVERIFY2(clearSquare(image, {0, 0}, scale), qPrintable(QString::number(resolution)));
            QVERIFY2(clearSquare(image, {resolution - scale, resolution - scale}, scale), qPrintable(QString::number(resolution)));
        }
    }

    // Tools and weapons take strokes, plants and the bow Scale2x; both bevel only their rim.
    void toolsTakeStrokes()
    {
        using QcTextureDetail::forPattern;
        using Pattern = QcTextureSpec::Pattern;
        for (const Pattern pattern : {Pattern::Tool, Pattern::Sword, Pattern::Pickaxe, Pattern::Axe, Pattern::Shovel, Pattern::Hoe, Pattern::Stick,
                                      Pattern::Arrow, Pattern::FishingRod}) {
            QVERIFY(forPattern(pattern).strokes);
            QVERIFY(forPattern(pattern).rimOnly);
        }
        QVERIFY(!forPattern(Pattern::Bow).strokes);
        QVERIFY(forPattern(Pattern::Bow).rimOnly);
        QVERIFY(!forPattern(Pattern::Bricks).rimOnly);
    }

    // The upper edge of a sword's blade is a straight 45 degree line at every resolution: each row
    // starts one column left of the row above. Scale2x left steps that doubled into bumps.
    void aBladeHasStraightEdges()
    {
        const QJsonObject sword = procedural("sword", "#d8d8d8", 0.08);
        for (const int resolution : {32, 64, 128}) {
            const QImage image = rendered(sword, resolution);
            QCOMPARE(image.width(), resolution);
            const int scale = resolution / 16;
            // Source rows 3 to 7 hold only the blade (the guard is below, the tip above).
            for (int y = 3 * scale; y < 7 * scale; ++y) {
                const std::optional<int> upper = firstVisible(image, y);
                const std::optional<int> lower = firstVisible(image, y + 1);
                QVERIFY(upper && lower);
                QVERIFY2(*upper - *lower == 1, qPrintable(u"%1: rows %2 and %3 start at %4 and %5"_s.arg(resolution).arg(y).arg(y + 1).arg(*upper).arg(*lower)));
            }
        }
    }

    // A pickaxe's handle reaches its head without a gap: the diagonal step between the last two
    // handle pixels (11, 4) and (12, 3) is covered although the head lies beyond it.
    void aHandleMeetsTheHead()
    {
        const QJsonObject pickaxe = procedural("pickaxe", "#d8d8d8", 0.08);
        for (const int resolution : {32, 64, 128}) {
            const QImage image = rendered(pickaxe, resolution);
            const int scale = resolution / 16;
            // The pixel whose centre lies just left of and above the middle of the step.
            QCOMPARE(image.pixelColor(12 * scale - 1, 4 * scale - 1).alpha(), 255);
        }
    }

    // Enlarged, the sword has a guard across the blade and a pommel; its 16 x 16 icon has none, and
    // stays as it was.
    void anEnlargedSwordHasAGuard()
    {
        const QJsonObject sword = procedural("sword", "#d8d8d8", 0.08);
        const QImage small = rendered(sword, 16);
        QCOMPARE(small.pixelColor(4, 8).alpha(), 0);
        QCOMPARE(small.pixelColor(0, 15).alpha(), 0);
        QVERIFY(small.pixelColor(6, 9).alpha() > 0);

        const QImage large = rendered(sword, 64);
        const QColor guard = large.pixelColor(4 * 4 + 2, 8 * 4 + 2);
        const QColor blade = large.pixelColor(10 * 4 + 2, 5 * 4 + 2);
        QCOMPARE(guard.alpha(), 255);
        QVERIFY2(guard.lightness() < blade.lightness() - 40, qPrintable(u"guard %1, blade %2"_s.arg(guard.lightness()).arg(blade.lightness())));
        QCOMPARE(large.pixelColor(0 * 4 + 2, 15 * 4 + 1).alpha(), 255);
        // The blade tapers to its tip (14, 1): the lower edge runs from the end of the lower row
        // (13, 3) to the tip instead of stepping in; this pixel lies in that step.
        QCOMPARE(large.pixelColor(56, 10).alpha(), 255);
    }

    // Enlarged, the axe's head is a bit flaring to the top left with a lit cutting edge and a
    // darker cheek at the haft; its 16 x 16 icon keeps the old square head.
    void anEnlargedAxeHasABit()
    {
        const QJsonObject axe = procedural("axe", "#d8d8d8", 0.08);
        const QImage small = rendered(axe, 16);
        QVERIFY(small.pixelColor(13, 1).alpha() > 0);
        QCOMPARE(small.pixelColor(3, 6).alpha(), 0);

        const QImage large = rendered(axe, 64);
        const auto at = [&large](int x, int y) { return large.pixelColor(x * 4 + 2, y * 4 + 2); };
        // The lower horn (the beard) and the upper one of the edge; the square head's corner is gone.
        QCOMPARE(at(2, 7).alpha(), 255);
        QCOMPARE(at(6, 1).alpha(), 255);
        QCOMPARE(at(13, 1).alpha(), 0);
        // The cutting edge is lit, the cheek at the haft darker than the bit.
        const QColor edge = at(4, 3);
        const QColor bit = at(5, 5);
        const QColor cheek = at(7, 6);
        QVERIFY2(edge.lightness() > bit.lightness() + 10, qPrintable(u"edge %1, bit %2"_s.arg(edge.lightness()).arg(bit.lightness())));
        QVERIFY2(cheek.lightness() < bit.lightness() - 40, qPrintable(u"cheek %1, bit %2"_s.arg(cheek.lightness()).arg(bit.lightness())));
        // The haft passes through the eye in the secondary colour.
        const QColor eye = at(8, 7);
        QVERIFY2(eye.red() > eye.blue() + 20, qPrintable(eye.name()));
    }

    // A picture without transparent pixels keeps every pixel; a single pixel becomes a disc.
    void outlinesOfSimpleShapes()
    {
        QImage full(16, 16, QImage::Format_RGBA8888);
        full.fill(QColor(90, 120, 30));
        const QImage large = full.scaled(64, 64);
        QCOMPARE(QcTextureOutline::cut(large, full), large);

        QImage dot(16, 16, QImage::Format_RGBA8888);
        dot.fill(Qt::transparent);
        dot.setPixelColor(8, 8, Qt::red);
        const QImage disc = QcTextureOutline::cut(dot.scaled(64, 64), dot);
        int visible = 0;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                visible += disc.pixelColor(x, y).alpha() > 0;
            }
        }
        // A disc of radius 2 pixels: 12 pixel centres lie within it (out of the cell's 16).
        QCOMPARE(visible, 12);
        QCOMPARE(disc.pixelColor(33, 33), QColor(Qt::red));
        QCOMPARE(disc.pixelColor(32, 32).alpha(), 0);
    }
};

QTEST_GUILESS_MAIN(TestTextureOutline)
#include "test_texture_outline.moc"
