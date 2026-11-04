// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcHeldItemMesher.h"
#include "clientcore/entitymeshes/QcSpriteShape.h"
#include "clientcore/entitymeshes/QcSpriteShapes.h"
#include "clientcore/figures/QcFigureLook.h"
#include "clientcore/texturepacks/QcTexturePackLibrary.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QImage>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <tuple>

using namespace Qt::StringLiterals;

namespace {

// A side as the test sees it: the drawn pixel and the way it faces (0 left, 1 right, 2 towards the
// top of the picture, 3 towards the bottom).
using PixelSide = std::tuple<int, int, int>;

constexpr double tolerance = 1.0e-6;

// The items whose sprites are checked: blades, a pick head, a bow and a stick.
QStringList testedItems()
{
    return {u"qtcraft:iron_sword"_s, u"qtcraft:iron_pickaxe"_s, u"qtcraft:bow"_s, u"qtcraft:stick"_s};
}

// Whether pixel (x, y) of `tile` is drawn by the alpha test; the outside is clear.
bool drawn(const QImage &tile, int x, int y)
{
    return x >= 0 && y >= 0 && x < tile.width() && y < tile.height() && qAlpha(tile.pixel(x, y)) >= QcSpriteShape::minOpaqueAlpha;
}

// Every side the picture needs, straight from its alpha mask: each drawn pixel next to a clear one.
QList<PixelSide> sidesOfMask(const QImage &tile)
{
    static constexpr std::array<std::array<int, 3>, 4> neighbours = {{{-1, 0, 0}, {1, 0, 1}, {0, -1, 2}, {0, 1, 3}}};
    QList<PixelSide> sides;
    for (int y = 0; y < tile.height(); ++y) {
        for (int x = 0; x < tile.width(); ++x) {
            for (const auto &[dx, dy, facing] : neighbours) {
                if (drawn(tile, x, y) && !drawn(tile, x + dx, y + dy)) {
                    sides.append({x, y, facing});
                }
            }
        }
    }
    std::ranges::sort(sides);
    return sides;
}

QcVec3 normalOf(const QcSpriteShape::FormQuad &quad)
{
    const QcVec3 a = quad.corners[1].point - quad.corners[0].point;
    const QcVec3 b = quad.corners[2].point - quad.corners[0].point;
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// The facing of an outward normal that points straight along x or y; none for any other.
std::optional<int> facingOf(const QcVec3 &normal)
{
    const bool alongX = std::abs(normal.y) < tolerance && std::abs(normal.z) < tolerance && std::abs(normal.x) > tolerance;
    const bool alongY = std::abs(normal.x) < tolerance && std::abs(normal.z) < tolerance && std::abs(normal.y) > tolerance;
    if (alongX) {
        return normal.x < 0.0 ? 0 : 1;
    }
    if (alongY) {
        return normal.y > 0.0 ? 2 : 3;
    }
    return std::nullopt;
}

struct SideCheck
{
    QList<PixelSide> sides;
    QStringList problems;
};

// One side quad of a form `n` pixels across, checked on its own: one straight edge facing along its
// normal, one base texel thick, its picture along the middle of its own pixels; with its pixels.
SideCheck checkSide(const QcSpriteShape::FormQuad &quad, int n)
{
    SideCheck check;
    const std::optional<int> facing = facingOf(normalOf(quad));
    if (!facing) {
        check.problems.append(u"a side's normal is not along x or y"_s);
        return check;
    }
    const bool vertical = *facing < 2;
    // Pixel coordinates (x right, y down) of the corners; the edge's line and its two ends.
    QList<double> along;
    double line = 0.0;
    for (const QcSpriteShape::FormCorner &corner : quad.corners) {
        const QPointF pixel((corner.point.x + 0.5) * n, (1.0 - corner.point.y) * n);
        along.append(vertical ? pixel.y() : pixel.x());
        line = vertical ? pixel.x() : pixel.y();
        if (std::abs(std::abs(corner.point.z) - 0.5 / QcSpriteShape::baseTexels) > tolerance) {
            check.problems.append(u"a side is not one base texel thick"_s);
        }
    }

    // The drawn pixels lie inside: after the line facing left or up, before it facing right or down.
    const int inner = static_cast<int>(std::lround(line)) - (*facing == 1 || *facing == 3 ? 1 : 0);
    for (int index = 0; index < 4; ++index) {
        const QPointF texture = quad.corners[static_cast<std::size_t>(index)].texture * n;
        const double textureAlong = vertical ? texture.y() : texture.x();
        const double textureAcross = vertical ? texture.x() : texture.y();
        if (std::abs(textureAlong - along[index]) > tolerance || std::abs(textureAcross - (inner + 0.5)) > tolerance) {
            check.problems.append(u"a side's picture is not the middle of its own pixels"_s);
        }
    }
    const auto [from, to] = std::ranges::minmax(along);
    for (int at = static_cast<int>(std::lround(from)); at < static_cast<int>(std::lround(to)); ++at) {
        check.sides.append(vertical ? PixelSide{inner, at, *facing} : PixelSide{at, inner, *facing});
    }
    return check;
}

// The front and the back: the whole picture, facing +z and -z.
QStringList checkFaces(const QList<QcSpriteShape::FormQuad> &form)
{
    QStringList problems;
    for (qsizetype index = 0; index < 2; ++index) {
        const QcVec3 normal = normalOf(form[index]);
        if (std::abs(normal.x) > tolerance || std::abs(normal.y) > tolerance || (index == 0) != (normal.z > 0.0)) {
            problems.append(u"the front or back does not face along z"_s);
        }
        for (const QcSpriteShape::FormCorner &corner : form[index].corners) {
            if (std::abs(corner.texture.x() - (corner.point.x + 0.5)) > tolerance || std::abs(corner.texture.y() - (1.0 - corner.point.y)) > tolerance) {
                problems.append(u"the front or back does not show the whole picture"_s);
            }
        }
    }
    return problems;
}

SideCheck checkForm(const QList<QcSpriteShape::FormQuad> &form, int n)
{
    SideCheck check;
    check.problems = checkFaces(form);
    const QList<QcSpriteShape::FormQuad> sides = form.sliced(2);
    for (const QcSpriteShape::FormQuad &quad : sides) {
        const SideCheck side = checkSide(quad, n);
        check.sides.append(side.sides);
        check.problems.append(side.problems);
    }
    std::ranges::sort(check.sides);
    return check;
}

} // namespace

// The solid form of a sprite follows the picture the GPU draws, in every pack (decision E-991).
class TestSpriteResolution : public QObject
{
    Q_OBJECT

    std::shared_ptr<QcContentRegistry> m_content = std::make_shared<QcContentRegistry>();
    QHash<int, std::shared_ptr<const QcTextureAtlas>> m_atlases;

    // The atlas at `resolution`: the game's own, or its HD pack painted at that size (the 128 pack
    // painted at 256 above that).
    [[nodiscard]] std::shared_ptr<const QcTextureAtlas> atlasAt(int resolution)
    {
        if (const auto found = m_atlases.constFind(resolution); found != m_atlases.constEnd()) {
            return *found;
        }
        QcTexturePackStack packs;
        if (resolution > 16) {
            const QList<QcTexturePackLibrary::Entry> entries = QcTexturePackLibrary::scan(QcTexturePackLibrary::roots(QString()));
            auto pack = std::make_shared<QcTexturePack>(*QcTexturePackLibrary::loadStack({u"builtin:qtcraft_hd_%1"_s.arg(std::min(resolution, 128))}, entries).stack.front());
            pack->manifest.resolution = resolution;
            packs.append(pack);
        }
        // Only the textures of the tested items: 256 pixel tiles of the whole content would not fit
        // the default budget, and the atlas stays small.
        QcContentRegistry content;
        const QStringList items = testedItems();
        for (const QString &name : items) {
            content.assets().registerTexture(*m_content->assets().texture(textureOf(name)));
        }
        const auto atlas = std::make_shared<const QcTextureAtlas>(QcTextureAtlas::build(content, packs));
        return *m_atlases.insert(resolution, atlas);
    }

    [[nodiscard]] QString textureOf(const QString &item) const { return m_content->item(*m_content->itemId(item)).texture; }

    // Where a fist holds `item` in the atlas at `resolution`, in units of the sprite.
    [[nodiscard]] QcVec3 gripAt(const QString &item, int resolution)
    {
        QcEntityMesher::ItemLook look = QcFigureLook::item(*m_content, *m_content->itemId(item));
        QcSpriteShapes shapes;
        look.shape = shapes.shape(atlasAt(resolution), look.sprite);
        return QcEntityMesher::spriteGrip(look, 1.0);
    }

    static void addResolutions()
    {
        QTest::addColumn<int>("resolution");
        for (const int resolution : {16, 32, 64, 128, 256}) {
            QTest::addRow("%d", resolution) << resolution;
        }
    }

private slots:
    void initTestCase()
    {
        QVERIFY(QcBaseContent::loadInto(*m_content));
        QVERIFY(m_content->finish().isEmpty());
    }

    void sidesFollowTheAlphaMask_data() { addResolutions(); }

    // Every side the mask needs and no other, each facing out of its pixel with that pixel's colour.
    void sidesFollowTheAlphaMask()
    {
        QFETCH(int, resolution);
        const std::shared_ptr<const QcTextureAtlas> atlas = atlasAt(resolution);
        QCOMPARE(atlas->tileSize(), resolution);

        QcSpriteShapes shapes;
        const QStringList items = testedItems();
        for (const QString &name : items) {
            const std::shared_ptr<const QcSpriteShape> shape = shapes.shape(atlas, textureOf(name));
            // Shapes stop at the cap; larger packs give the mip level of that size.
            QCOMPARE(shape->size(), std::min(resolution, QcSpriteShapes::maxTexels));
            const QImage tile = atlas->tile(textureOf(name), shape->size()).convertToFormat(QImage::Format_ARGB32);
            QCOMPARE(tile.width(), shape->size());

            const QList<PixelSide> expected = sidesOfMask(tile);
            const SideCheck check = checkForm(shape->form(), shape->size());
            qInfo().noquote() << u"FORM resolution=%1 item=%2 shape=%3 quads=%4 side_pixels=%5 mask_sides=%6"_s.arg(resolution)
                                     .arg(name)
                                     .arg(shape->size())
                                     .arg(shape->form().size())
                                     .arg(check.sides.size())
                                     .arg(expected.size());
            QVERIFY(!expected.isEmpty());
            QVERIFY2(check.problems.isEmpty(), qPrintable(check.problems.value(0)));
            QCOMPARE(check.sides, expected);
        }
    }

    void gripSitsAsInTheStandardPack_data() { addResolutions(); }

    // The fist holds a tool at the same place on its handle in every resolution: at most half a
    // base texel from where it holds the game's own 16 pixel sprite.
    void gripSitsAsInTheStandardPack()
    {
        QFETCH(int, resolution);
        for (const QString &name : {u"qtcraft:iron_sword"_s, u"qtcraft:iron_pickaxe"_s, u"qtcraft:stick"_s}) {
            QVERIFY(QcFigureLook::item(*m_content, *m_content->itemId(name)).grip != QcHoldGrip::Middle);
            const QcVec3 standard = gripAt(name, 16);
            const QcVec3 grip = gripAt(name, resolution);
            const double dx = (grip.x - standard.x) * QcSpriteShape::baseTexels;
            const double dy = (grip.y - standard.y) * QcSpriteShape::baseTexels;
            qInfo().noquote() << u"GRIP resolution=%1 item=%2 dx=%3 dy=%4 (base texels)"_s.arg(resolution).arg(name).arg(dx, 0, 'f', 3).arg(dy, 0, 'f', 3);
            QVERIFY2(std::abs(dx) <= 0.5 && std::abs(dy) <= 0.5, qPrintable(name));
        }
    }
};

QTEST_GUILESS_MAIN(TestSpriteResolution)
#include "test_sprite_resolution.moc"
