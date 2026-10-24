#include "clientcore/textures/QcProceduralTexture.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "clientcore/textures/QcTextureSynthesizer.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcContentRegistry.h"
#include "QcTestUtil.h"

#include <QSet>
#include <QTest>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// The cow's colours (content/qtcraft/textures/cow_hide.json): white with dark patches.
const QColor white(0xee, 0xea, 0xe2);
const QColor dark(0x2e, 0x26, 0x20);

QcTextureSpec patches(quint32 seed)
{
    return QcTextureSpec{QcTextureSpec::Pattern::Patches, white, dark, 0.1, seed};
}

bool isPatch(const QImage &image, int x, int y)
{
    return qGray(image.pixel(x, y)) < 128;
}

// Sizes of the patches: groups of dark pixels joined through their four neighbours.
QList<int> patchSizes(const QImage &image)
{
    const int size = image.width();
    const int cells = size * size;
    QList<bool> seen(cells, false);
    QList<int> sizes;
    for (int start = 0; start < cells; ++start) {
        if (seen[start] || !isPatch(image, start % size, start / size)) {
            continue;
        }
        QList<int> open{start};
        seen[start] = true;
        int count = 0;
        while (!open.isEmpty()) {
            const int at = open.takeLast();
            ++count;
            const int x = at % size;
            const int y = at / size;
            for (const auto &[dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= size || ny >= size || seen[ny * size + nx] || !isPatch(image, nx, ny)) {
                    continue;
                }
                seen[ny * size + nx] = true;
                open.append(ny * size + nx);
            }
        }
        sizes.append(count);
    }
    return sizes;
}

int texelsBelowFullAlpha(const QImage &tile)
{
    const QImage image = tile.convertToFormat(QImage::Format_RGBA8888);
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            count += image.pixelColor(x, y).alpha() < 255 ? 1 : 0;
        }
    }
    return count;
}

} // namespace

class TestMobTextures : public QObject
{
    Q_OBJECT

private slots:
    // The coat pattern of the cow (decision E-880): a few big patches per face instead of single
    // dots, the same for the same seed, elsewhere for another one, fully opaque.
    void patchesAreFewLargeAndDeterministic_data()
    {
        QTest::addColumn<quint32>("seed");
        for (const quint32 seed : {7101u, 7102u, 7106u, 1u, 2u}) {
            QTest::newRow(qPrintable(QString::number(seed))) << seed;
        }
        const auto random = static_cast<quint32>(QcTestUtil::randomSeed());
        QTest::newRow(qPrintable(u"random %1"_s.arg(random))) << random;
    }

    void patchesAreFewLargeAndDeterministic()
    {
        QFETCH(quint32, seed);
        QCOMPARE(qcTexturePatternFromName(u"patches"_s), QcTextureSpec::Pattern::Patches);
        const QImage image = QcTextureSynthesizer::synthesize(patches(seed));
        QCOMPARE(image, QcTextureSynthesizer::synthesize(patches(seed)));
        QVERIFY(image != QcTextureSynthesizer::synthesize(patches(seed + 1)));
        QCOMPARE(texelsBelowFullAlpha(image), 0);

        const QList<int> sizes = patchSizes(image);
        int covered = 0;
        for (const int size : sizes) {
            covered += size;
        }
        const double share = static_cast<double>(covered) / (image.width() * image.height());
        QVERIFY2(share > 0.15 && share < 0.65, qPrintable(u"share %1"_s.arg(share)));
        QVERIFY2(sizes.size() <= 10, qPrintable(u"%1 patches"_s.arg(sizes.size())));
        QVERIFY2(*std::ranges::max_element(sizes) >= 16, qPrintable(u"largest %1"_s.arg(*std::ranges::max_element(sizes))));

        // The speckle pattern the cow had before scatters dozens of single dots.
        QcTextureSpec speckle = patches(seed);
        speckle.pattern = QcTextureSpec::Pattern::Speckle;
        QVERIFY(patchSizes(QcTextureSynthesizer::synthesize(speckle)).size() > 2 * sizes.size());
    }

    void cowHideHeadAndLegsHavePatches()
    {
        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        for (const QString &name : {u"qtcraft:cow_hide"_s, u"qtcraft:cow_head"_s, u"qtcraft:cow_leg"_s}) {
            const QcTextureDefinition *definition = content->registry->assets().texture(name);
            QVERIFY2(definition, qPrintable(name));
            const QcResult<QcTextureSpec> spec = QcProceduralTexture::spec(definition->procedural);
            QVERIFY2(spec.has_value(), qPrintable(name));
            QCOMPARE(spec->pattern, QcTextureSpec::Pattern::Patches);
        }
    }

    // Creatures are drawn with an alpha test (entity.frag discards texels below 0.1): a transparent
    // texel in a body texture is a hole through which the inside and the far side show. The smith's
    // apron once used the leather item icon, whose transparent margin made the torso look half
    // transparent. Only the textures made to be see-through may have such texels: the eyes in front
    // of a head (slot "face"), the skeleton's ribs and the Kjut's eye ring.
    void bodyTexturesOfCreaturesAreOpaque()
    {
        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        const QcContentRegistry &registry = *content->registry;
        const QcTextureAtlas atlas = QcTextureAtlas::build(registry);
        const QSet<QString> seeThrough{u"qtcraft:skeleton_ribs"_s, u"qtcraft:kjut_eye_ring"_s};

        QList<std::pair<QString, QString>> uses;
        for (const QcMobDefinition &mob : registry.mobs()) {
            for (const QcMobPart &part : mob.parts) {
                uses.append({mob.name, part.texture});
            }
        }
        for (const QcAppearanceDefinition &appearance : registry.assets().appearances()) {
            for (const QcAppearanceLayer &layer : appearance.layers) {
                if (layer.slot != u"face"_s) {
                    uses.append({appearance.name + u" "_s + layer.slot, layer.texture});
                }
            }
        }

        int checked = 0;
        for (const auto &[user, texture] : std::as_const(uses)) {
            if (seeThrough.contains(texture)) {
                continue;
            }
            QVERIFY2(registry.assets().texture(texture), qPrintable(user + u": unknown "_s + texture));
            ++checked;
            QVERIFY2(texelsBelowFullAlpha(atlas.tile(texture)) == 0, qPrintable(user + u": "_s + texture));
        }
        QVERIFY(checked > 100);

        // The smith's outfit lays the apron over the shirt.
        const auto smith = std::ranges::find_if(registry.assets().appearances(), [](const QcAppearanceDefinition &appearance) {
            return appearance.name.endsWith(u"@qtcraft:smith"_s) || appearance.name.endsWith(u"@smith"_s);
        });
        QVERIFY(smith != registry.assets().appearances().end());
        QVERIFY(smith->layer(u"torso"_s));
        QCOMPARE(smith->layer(u"torso"_s)->texture, u"qtcraft:villager_smith_apron"_s);
    }
};

QTEST_GUILESS_MAIN(TestMobTextures)
#include "test_mob_textures.moc"
