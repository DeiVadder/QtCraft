#include "clientcore/textures/QcTextureSynthesizer.h"

#include <QTest>

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
