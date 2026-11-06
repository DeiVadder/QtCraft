// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The relief patterns (decision E-1043): their fields repeat with the tile, every surface joins
// itself across both edges of the tile, a texture is painted at each resolution instead of being
// enlarged, the same seed gives the same picture, and the Nether's materials differ from each
// other in structure, not only in colour.

#include "clientcore/materials/QcMaterialRules.h"
#include "clientcore/materials/QcMaterialSynthesizer.h"
#include "clientcore/textures/QcProceduralTexture.h"
#include "clientcore/textures/QcTextureField.h"
#include "clientcore/textures/QcTextureRelief.h"
#include "common/content/blocks/QcTextureDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "fixtures/QcTestUtil.h"

#include <QImage>
#include <QJsonObject>
#include <QTest>

#include <array>
#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;
using namespace QcTextureField;

namespace {

constexpr std::array patterns{"fused_rock", "hollow_sand", "crusted_soil", "column_side", "column_top",
                              "polished_columns", "flow_bands", "glassy_stone", "polished_glass", "gilded_stone",
                              "ashlar", "cracked_ashlar", "carved_ashlar", "fired_bricks", "cracked_fired_bricks",
                              "carved_fired_bricks", "quartz_ore", "gold_vein_ore", "magma_crust", "glow_cluster"};

QJsonObject definition(const QString &pattern, int seed = 8401)
{
    return {{u"pattern"_s, pattern}, {u"primary"_s, u"#7a5040"_s}, {u"secondary"_s, u"#201810"_s}, {u"variation"_s, 0.08}, {u"seed"_s, seed}};
}

QImage rendered(const QJsonObject &texture, int resolution)
{
    const QcResult<QImage> image = QcProceduralTexture::render(texture, resolution);
    return image ? image->convertToFormat(QImage::Format_RGBA8888) : QImage();
}

double brightness(const QImage &image, int x, int y)
{
    return qGray(image.pixel(x, y)) / 255.0;
}

// The picture's brightness with its average taken out and its spread scaled to one, so two
// pictures compare by their structure alone.
QList<double> structure(const QImage &image)
{
    QList<double> values;
    double sum = 0.0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            values.append(brightness(image, x, y));
            sum += values.back();
        }
    }
    const double mean = sum / static_cast<double>(values.size());
    double spread = 0.0;
    for (double &value : values) {
        value -= mean;
        spread += value * value;
    }
    spread = std::sqrt(spread / static_cast<double>(values.size()));
    for (double &value : values) {
        value /= spread;
    }
    return values;
}

// The picture's autocorrelation at a few shifts across and down: how far its features reach and
// in which direction. It does not depend on where the features lie, so two seeds of one pattern
// come out alike and two materials do not.
QList<double> autocorrelation(const QImage &image)
{
    const QList<double> values = structure(image);
    const int size = image.width();
    QList<double> result;
    for (const int shift : {1, 2, 3, 4, 6, 8}) {
        double across = 0.0;
        double down = 0.0;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const double value = values[y * size + x];
                across += value * values[y * size + (x + shift) % size];
                down += value * values[(y + shift) % size * size + x];
            }
        }
        result.append(across / static_cast<double>(values.size()));
        result.append(down / static_cast<double>(values.size()));
    }
    return result;
}

double distance(const QList<double> &first, const QList<double> &second)
{
    double sum = 0.0;
    for (qsizetype i = 0; i < first.size(); ++i) {
        sum += (first[i] - second[i]) * (first[i] - second[i]);
    }
    return std::sqrt(sum);
}

// The difference of two points of a surface in height and colour.
double jump(const QcTextureRelief::Surface &first, const QcTextureRelief::Surface &second)
{
    const int colour = std::abs(first.colour.red() - second.colour.red()) + std::abs(first.colour.green() - second.colour.green())
                       + std::abs(first.colour.blue() - second.colour.blue());
    return colour + 40.0 * std::abs(first.height - second.height);
}

// How many of the points along a line of the tile jump across it: `line` is a place across the
// tile; `vertical` lines run down. A line at 0 compares the last point of the tile with the first.
int jumpsAcross(const QcTextureSpec &spec, double line, bool vertical)
{
    constexpr double epsilon = 1e-6;
    constexpr double visible = 12.0;
    const double before = line == 0.0 ? 16.0 - epsilon : line - epsilon;
    int jumps = 0;
    for (int step = 0; step < 160; ++step) {
        const double along = (step + 0.5) / 10.0;
        const QPointF first = vertical ? QPointF(before, along) : QPointF(along, before);
        const QPointF second = vertical ? QPointF(line + epsilon, along) : QPointF(along, line + epsilon);
        jumps += jump(QcTextureRelief::surfaceAt(spec, first), QcTextureRelief::surfaceAt(spec, second)) > visible ? 1 : 0;
    }
    return jumps;
}

} // namespace

class TestTextureRelief : public QObject
{
    Q_OBJECT

private slots:
    void fieldsRepeatWithTheTile()
    {
        const QPointF point(3.3, 11.7);
        const QPointF nextTile = point + QPointF(16.0, -16.0);
        QCOMPARE(noise(5, point, 4.0), noise(5, nextTile, 4.0));
        QCOMPARE(fractal(5, point, 8.0, 3), fractal(5, nextTile, 8.0, 3));
        const Grid grid{4, 4, 0.9, false};
        QCOMPARE(cellAt(9, point, grid).id, cellAt(9, nextTile, grid).id);
        QVERIFY(std::abs(cellAt(9, point, grid).edge - cellAt(9, nextTile, grid).edge) < 1e-9);
    }

    void aCellHasOneIdAndItsEdgeIsNeverNegative()
    {
        const Grid staggered{3, 4, 0.3, true};
        for (int y = 0; y < 43; ++y) {
            for (int x = 0; x < 43; ++x) {
                const Cell cell = cellAt(2, QPointF(x * 0.37, y * 0.37), staggered);
                QVERIFY(cell.edge >= -1e-9);
                QCOMPARE(cellAt(2, cell.centre, staggered).id, cell.id);
            }
        }
    }

    void smoothAndMixedStayInTheirRange()
    {
        QCOMPARE(smooth(1.0, 2.0, 0.0), 0.0);
        QCOMPARE(smooth(1.0, 2.0, 3.0), 1.0);
        QCOMPARE(smooth(1.0, 2.0, 1.5), 0.5);
        QCOMPARE(mixed(QColor(0, 0, 0), QColor(200, 100, 50), 0.5), QColor(100, 50, 25));
        QCOMPARE(mixed(QColor(0, 0, 0), QColor(200, 100, 50), 2.0), QColor(200, 100, 50));
    }

    void everyPatternIsPaintedAtEachResolution_data()
    {
        QTest::addColumn<QString>("pattern");
        for (const char *pattern : patterns) {
            QTest::newRow(pattern) << QString::fromLatin1(pattern);
        }
    }

    void everyPatternIsPaintedAtEachResolution()
    {
        QFETCH(QString, pattern);
        const std::optional<QcTextureSpec::Pattern> known = qcTexturePatternFromName(pattern);
        QVERIFY(known.has_value());
        QVERIFY(QcTextureRelief::isRelief(*known));
        for (const int resolution : {16, 32, 64}) {
            const QImage image = rendered(definition(pattern), resolution);
            QCOMPARE(image.size(), QSize(resolution, resolution));
            QCOMPARE(image, rendered(definition(pattern), resolution));
            QVERIFY(image != rendered(definition(pattern, 8402), resolution));
            for (int i = 0; i < resolution; ++i) {
                QCOMPARE(qAlpha(image.pixel(i, resolution - 1 - i)), 255);
            }
        }
    }

    void texturesJoinWithoutASeam_data()
    {
        everyPatternIsPaintedAtEachResolution_data();
    }

    void texturesJoinWithoutASeam()
    {
        // A cell border can cross any line, so a line inside the tile is the measure: the tile's
        // edges must show no more jumps than it does, for any seed.
        QFETCH(QString, pattern);
        QcTextureSpec spec;
        spec.pattern = *qcTexturePatternFromName(pattern);
        spec.primary = QColor(122, 80, 64);
        spec.secondary = QColor(32, 24, 16);
        for (quint32 seed = 0; seed < 24; ++seed) {
            spec.seed = seed * 977u + 11u;
            for (const bool vertical : {true, false}) {
                const int inside = jumpsAcross(spec, 8.0, vertical);
                const int edge = jumpsAcross(spec, 0.0, vertical);
                QVERIFY2(edge <= inside + 2, qPrintable(u"%1 seed %2 vertical %3: %4 jumps at the edge, %5 inside"_s.arg(pattern).arg(spec.seed).arg(vertical).arg(edge).arg(inside)));
            }
        }
    }

    void aPaintedTextureIsNotAnEnlargedOne()
    {
        // Without grain and variation an enlarged 16 x 16 picture is blocks of 8 x 8 equal pixels;
        // a painted one has its cracks and tones inside them.
        QJsonObject texture = definition(u"crusted_soil"_s);
        texture.insert(u"variation"_s, 0.0);
        texture.insert(u"detail"_s, QJsonObject{{u"grain"_s, 0.0}});
        const QImage image = rendered(texture, 128);
        int blocksWithDetail = 0;
        for (int y = 0; y < 128; y += 8) {
            for (int x = 0; x < 128; x += 8) {
                blocksWithDetail += image.pixel(x + 1, y + 1) != image.pixel(x + 6, y + 6) ? 1 : 0;
            }
        }
        QVERIFY(blocksWithDetail > 100);
    }

    void theNetherMaterialsDifferInStructure()
    {
        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        const QStringList names{u"qtcraft:netherrack"_s, u"qtcraft:soul_sand"_s, u"qtcraft:soul_soil"_s, u"qtcraft:basalt_side"_s,
                                u"qtcraft:basalt_top"_s, u"qtcraft:blackstone"_s, u"qtcraft:magma_block"_s, u"qtcraft:glowstone"_s,
                                u"qtcraft:nether_bricks"_s, u"qtcraft:polished_blackstone_bricks"_s};
        QList<QList<double>> pictures;
        for (const QString &name : names) {
            bool found = false;
            for (const QcTextureDefinition &texture : content->registry->assets().textures()) {
                if (texture.name == name) {
                    pictures.append(autocorrelation(rendered(texture.procedural, 64)));
                    found = true;
                }
            }
            QVERIFY2(found, qPrintable(name));
        }
        for (qsizetype first = 0; first < pictures.size(); ++first) {
            for (qsizetype second = first + 1; second < pictures.size(); ++second) {
                const double apart = distance(pictures[first], pictures[second]);
                QVERIFY2(apart > 0.25, qPrintable(names[first] + u" / "_s + names[second]));
            }
        }
    }

    void twoSeedsOfOnePatternAreAlike()
    {
        // The control of the test above: the measure sees the pattern, not the seed.
        double sum = 0.0;
        for (const char *pattern : patterns) {
            const QString name = QString::fromLatin1(pattern);
            sum += distance(autocorrelation(rendered(definition(name, 1), 64)), autocorrelation(rendered(definition(name, 2), 64)));
        }
        QVERIFY(sum / static_cast<double>(patterns.size()) < 0.25);
    }

    void materialsFollowThePattern()
    {
        const auto material = [](const char *name, const char *pattern) {
            return QcMaterialRules::forTexture(QString::fromLatin1(name), definition(QString::fromLatin1(pattern)), false);
        };
        QVERIFY(material("qtcraft:glassy", "glassy_stone").smoothness > material("qtcraft:rock", "fused_rock").smoothness);

        // The pattern says what is in the stone, whatever the texture is called.
        QCOMPARE(material("mod:rich_vein", "gold_vein_ore").inclusion->metal, QcMaterialParams::Metal::Gold);
        QCOMPARE(material("mod:shiny", "gilded_stone").inclusion->metal, QcMaterialParams::Metal::Gold);
        QCOMPARE(material("mod:white_veins", "quartz_ore").inclusion->metal, QcMaterialParams::Metal::None);
        QVERIFY(material("mod:white_veins", "quartz_ore").inclusion->smoothness > 0.7f);

        // A word of the name does not change the finish of a relief pattern.
        QCOMPARE(material("mod:polished_rock", "fused_rock").smoothness, material("mod:rock", "fused_rock").smoothness);
    }

    void onlyTheCracksOfMagmaGlow()
    {
        const QJsonObject texture{{u"pattern"_s, u"magma_crust"_s}, {u"primary"_s, u"#2c1410"_s}, {u"secondary"_s, u"#f07820"_s},
                                  {u"variation"_s, 0.08}, {u"seed"_s, 903}};
        const QImage colour = rendered(texture, 64);
        const QcMaterialParams params = QcMaterialRules::forTexture(u"qtcraft:magma_block"_s, texture, true);
        const QImage specular = QcMaterialSynthesizer::paint(colour, params, {QColor(0x2c1410), QColor(0xf07820)}).specular;
        int glowing = 0;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const bool glows = qAlpha(specular.pixel(x, y)) > 0;
                glowing += glows ? 1 : 0;
                QVERIFY(!glows || qRed(colour.pixel(x, y)) > 100);
            }
        }
        QVERIFY(glowing > 64 * 64 / 20);
        QVERIFY(glowing < 64 * 64 / 2);
    }
};

QTEST_GUILESS_MAIN(TestTextureRelief)
#include "test_texture_relief.moc"
