// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcEntityMesher.h"
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
#include <numbers>
#include <optional>
#include <span>
#include <tuple>

using namespace Qt::StringLiterals;

namespace {

// A side as the test sees it: the drawn pixel and the way it faces (0 left, 1 right, 2 towards the
// top of the picture, 3 towards the bottom).
using PixelSide = std::tuple<int, int, int>;

constexpr double tolerance = 1.0e-6;

// The items whose sprites are checked: a blade with a pommel, a pick head, axe heads, a shovel, a
// hoe, a bow and a stick.
QStringList testedItems()
{
    return {u"qtcraft:iron_sword"_s, u"qtcraft:iron_pickaxe"_s, u"qtcraft:iron_axe"_s, u"qtcraft:diamond_axe"_s,
            u"qtcraft:iron_shovel"_s, u"qtcraft:iron_hoe"_s, u"qtcraft:bow"_s, u"qtcraft:stick"_s};
}

// The cross product of `a` and `b`.
QcVec3 cross(const QcVec3 &a, const QcVec3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Where the line of sight from a camera at the origin through `target` meets the plane of a
// held sprite's front (`front`: its four vertices, found by their texture coordinates in the unit
// square), in texels of a sprite `n` texels across (x from the left, y from the top).
QPointF texelSeen(std::span<const QcEntityVertex> front, const QcVec3 &target, int n)
{
    const auto corner = [&](float u, float v) {
        const auto found = std::ranges::find_if(front, [&](const QcEntityVertex &w) { return w.u == u && w.v == v; });
        return QcVec3{found->x, found->y, found->z};
    };
    const QcVec3 origin = corner(0.0f, 0.0f);
    const QcVec3 across = corner(1.0f, 0.0f) - origin;
    const QcVec3 down = corner(0.0f, 1.0f) - origin;
    // origin + a * across + b * down = s * target, solved by Cramer's rule.
    const double det = cross(across, down).dot(target);
    const double a = cross(down, target).dot(QcVec3{} - origin) / det;
    const double b = cross(target, across).dot(QcVec3{} - origin) / det;
    return {a * n, b * n};
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


// Where a fist holds a tool's sprite, in base texels along its handle line (the diagonal through
// the grip, from the lower left to the upper right): `below` how far the fist lies below where
// the handle enters the head (or a sword's guard), `haft` how far the fist lies from the nearest
// opaque texel.
struct HandleMeasure
{
    double below = 0.0;
    double haft = 0.0;
};

// Measures the fist seen at `fist` (texels) on `shape`. The head begins at the lowest texel above
// the grip that lies one to three base texels beside the handle line: beside the handle itself
// but near it, so the far corners of a broad axe blade or of a pick's prongs, which hang down
// beside the handle, do not count; a pommel lies below the grip. Without a head (a sword without
// a guard), `below` is huge.
HandleMeasure measureHandle(const QcSpriteShape &shape, QPointF fist)
{
    const double base = shape.texelsPerBase();
    const QPointF grip = shape.handlePoint();
    const auto along = [&](QPointF p) { return ((p.x() - grip.x()) - (p.y() - grip.y())) / (std::numbers::sqrt2 * base); };
    const auto aside = [&](QPointF p) { return std::abs((p.x() - grip.x()) + (p.y() - grip.y())) / (std::numbers::sqrt2 * base); };

    double head = 1e9;
    double haft = 1e9;
    for (int y = 0; y < shape.size(); ++y) {
        for (int x = 0; x < shape.size(); ++x) {
            if (!shape.opaque(x, y)) {
                continue;
            }
            const QPointF centre(x + 0.5, y + 0.5);
            haft = std::min(haft, std::hypot(centre.x() - fist.x(), centre.y() - fist.y()) / base);
            if (along(centre) > 0.0 && aside(centre) > 1.0 && aside(centre) <= 3.0) {
                head = std::min(head, along(centre));
            }
        }
    }
    return {head - along(fist), haft};
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
        for (const QString &name : {u"qtcraft:iron_sword"_s, u"qtcraft:iron_pickaxe"_s, u"qtcraft:iron_axe"_s, u"qtcraft:diamond_axe"_s, u"qtcraft:stick"_s}) {
            QVERIFY(QcFigureLook::item(*m_content, *m_content->itemId(name)).grip != QcHoldGrip::Middle);
            const QcVec3 standard = gripAt(name, 16);
            const QcVec3 grip = gripAt(name, resolution);
            const double dx = (grip.x - standard.x) * QcSpriteShape::baseTexels;
            const double dy = (grip.y - standard.y) * QcSpriteShape::baseTexels;
            qInfo().noquote() << u"GRIP resolution=%1 item=%2 dx=%3 dy=%4 (base texels)"_s.arg(resolution).arg(name).arg(dx, 0, 'f', 3).arg(dy, 0, 'f', 3);
            QVERIFY2(std::abs(dx) <= 0.5 && std::abs(dy) <= 0.5, qPrintable(name));
        }
    }
    void firstPersonFistHoldsTheHaft_data()
    {
        QTest::addColumn<int>("resolution");
        for (const int resolution : {16, 64}) {
            QTest::addRow("%d", resolution) << resolution;
        }
    }

    // In the first person the fist holds a tool by its handle, well below the head (or a sword's
    // guard): at rest the fist (QcEntityMesher::heldGrip) is seen on an opaque texel of the handle
    // at least one base texel below the head's lowest texel, in the game's own sprites and in the
    // HD pack (decision E-1000).
    void firstPersonFistHoldsTheHaft()
    {
        QFETCH(int, resolution);
        const QcEntityMesher::ModelLook player = QcFigureLook::player(*m_content, {});
        const auto unitUv = [](const QString &) { return QRectF(0.0, 0.0, 1.0, 1.0); };
        for (const QString &name : {u"qtcraft:iron_axe"_s, u"qtcraft:iron_sword"_s, u"qtcraft:iron_pickaxe"_s, u"qtcraft:iron_shovel"_s, u"qtcraft:iron_hoe"_s}) {
            QcEntityMesher::ItemLook look = QcFigureLook::item(*m_content, *m_content->itemId(name));
            QcSpriteShapes shapes;
            look.shape = shapes.shape(atlasAt(resolution), look.sprite);
            QcEntityMesh mesh;
            QcEntityMesher::appendHand(mesh, {player, &look}, {}, {{}, 0.0, 0.0, 1.0f}, unitUv);
            // The item comes last; its first quad is the front.
            const qsizetype front = mesh.vertices.size() - look.shape->form().size() * 4;
            const std::span<const QcEntityVertex> corners(mesh.vertices.constData() + front, 4);
            const QPointF fist = texelSeen(corners, QcEntityMesher::heldGrip, look.shape->size());

            const HandleMeasure measure = measureHandle(*look.shape, fist);
            qInfo().noquote() << u"FIST resolution=%1 item=%2 fist=(%3, %4) below_head=%5 to_haft=%6 (base texels)"_s.arg(resolution)
                                     .arg(name)
                                     .arg(fist.x(), 0, 'f', 1)
                                     .arg(fist.y(), 0, 'f', 1)
                                     .arg(measure.below, 0, 'f', 2)
                                     .arg(measure.haft, 0, 'f', 2);
            QVERIFY2(measure.haft <= 0.75, qPrintable(name));
            QVERIFY2(measure.below >= 1.0, qPrintable(name));
            // Every tool has a head; the game's own sword has no guard, its HD one has.
            QVERIFY2(measure.below < 1e6 || (name == u"qtcraft:iron_sword"_s && resolution == 16), qPrintable(name));
        }
    }
};

QTEST_GUILESS_MAIN(TestSpriteResolution)
#include "test_sprite_resolution.moc"
