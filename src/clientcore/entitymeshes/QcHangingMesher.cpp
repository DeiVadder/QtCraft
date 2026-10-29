// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcHangingMesher.h"

#include "clientcore/figures/QcFigureLook.h"

#include <QtMath>

#include <cmath>

namespace {

QcVec3 vectorOf(QcFaceOffset offset)
{
    return {static_cast<double>(offset.dx), static_cast<double>(offset.dy), static_cast<double>(offset.dz)};
}

QcVec3 cross(const QcVec3 &a, const QcVec3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// The wall's own axes: `right` and `up` as seen from the front, `out` away from the wall, and
// `origin`, the lower left corner of the hanging's first cell on the wall.
struct Axes
{
    QcVec3 right;
    QcVec3 up;
    QcVec3 out;
    QcVec3 origin;

    explicit Axes(const QcHanging &hanging)
        : right(vectorOf(QcHanging::rightOf(hanging.face)))
        , out(vectorOf(qcFaceOffset(hanging.face)))
    {
        up = cross(out, right);
        const QcVec3 centre{hanging.cell.x + 0.5, hanging.cell.y + 0.5, hanging.cell.z + 0.5};
        origin = centre - out * 0.5 - right * 0.5 - up * 0.5;
    }

    // A point `a` to the right, `b` up and `c` out from the origin.
    QcVec3 at(double a, double b, double c) const { return origin + right * a + up * b + out * c; }
};

// A board on the wall from `a0, b0` to `a1, b1` (right and up from the origin), standing `depth`
// out from the wall.
struct Panel
{
    double a0 = 0.0;
    double b0 = 0.0;
    double a1 = 1.0;
    double b1 = 1.0;
    double depth = 0.0;
};

// The corners as vertices of `mesh` (relative to its origin), not yet added.
std::array<QcEntityVertex, 4> quad(const QcEntityMesh &mesh, const std::array<QcVec3, 4> &corners, const QRectF &rect, QcEntityLight light,
                                   float shade)
{
    std::array<QcEntityVertex, 4> vertices;
    for (std::size_t corner = 0; corner < 4; ++corner) {
        QcEntityVertex vertex = mesh.vertexAt(corners[corner]);
        // Corners run bottom left, bottom right, top right, top left.
        vertex.u = static_cast<float>((corner == 0 || corner == 3) ? rect.left() : rect.right());
        vertex.v = static_cast<float>(corner < 2 ? rect.bottom() : rect.top());
        vertex.shade = shade;
        vertex.skyLight = light.sky;
        vertex.blockLight = light.block;
        vertices[corner] = vertex;
    }
    return vertices;
}

// The front of a board.
void boardFront(QcEntityMesh &mesh, const Axes &axes, const Panel &panel, const QRectF &front, QcEntityLight light)
{
    const double depth = panel.depth;
    const auto p = [&](double a, double b, double c) { return axes.at(a, b, c); };
    mesh.addQuad(quad(mesh, {p(panel.a0, panel.b0, depth), p(panel.a1, panel.b0, depth), p(panel.a1, panel.b1, depth), p(panel.a0, panel.b1, depth)}, front, light, 1.0f));
}

// The four edges of a board from its front back to the wall.
void boardEdges(QcEntityMesh &mesh, const Axes &axes, const Panel &panel, const QRectF &edge, QcEntityLight light)
{
    const double depth = panel.depth;
    const auto p = [&](double a, double b, double c) { return axes.at(a, b, c); };
    mesh.addQuad(quad(mesh, {p(panel.a0, panel.b0, 0.0), p(panel.a1, panel.b0, 0.0), p(panel.a1, panel.b0, depth), p(panel.a0, panel.b0, depth)}, edge, light, 0.7f), true);
    mesh.addQuad(quad(mesh, {p(panel.a0, panel.b1, depth), p(panel.a1, panel.b1, depth), p(panel.a1, panel.b1, 0.0), p(panel.a0, panel.b1, 0.0)}, edge, light, 0.9f), true);
    mesh.addQuad(quad(mesh, {p(panel.a0, panel.b0, 0.0), p(panel.a0, panel.b0, depth), p(panel.a0, panel.b1, depth), p(panel.a0, panel.b1, 0.0)}, edge, light, 0.8f), true);
    mesh.addQuad(quad(mesh, {p(panel.a1, panel.b0, depth), p(panel.a1, panel.b0, 0.0), p(panel.a1, panel.b1, 0.0), p(panel.a1, panel.b1, depth)}, edge, light, 0.8f), true);
}

void appendFrame(QcEntityMesh &mesh, const QcClientEntity &entity, const QcContentRegistry &content, QcEntityLight light,
                 const QcEntityMesher::UvLookup &uv)
{
    const Axes axes(entity.hanging);
    const QRectF texture = uv(content.item(entity.held).texture);
    // A map covers the whole face; only the edges of the board show, the map is drawn on its own.
    if (QcHangingMesher::framedMap(entity, content)) {
        boardEdges(mesh, axes, {0.0, 0.0, 1.0, 1.0, QcHanging::frameDepth}, texture, light);
        return;
    }
    const double inset = (1.0 - QcHanging::frameSide) * 0.5;
    const Panel panel{inset, inset, 1.0 - inset, 1.0 - inset, QcHanging::frameDepth};
    boardFront(mesh, axes, panel, texture, light);
    boardEdges(mesh, axes, panel, texture, light);
    if (entity.item.isEmpty() || !content.isValidItem(entity.item.item)) {
        return;
    }

    // The item lies on the frame, turned an eighth per step clockwise as seen from the front.
    const QcEntityMesher::ItemLook look = QcFigureLook::item(content, entity.item.item);
    const double angle = -qDegreesToRadians(45.0 * entity.hanging.rotation);
    const QcVec3 itemRight = axes.right * std::cos(angle) + axes.up * std::sin(angle);
    const QcVec3 itemUp = axes.up * std::cos(angle) - axes.right * std::sin(angle);
    const double lift = look.cube ? QcEntityMesher::itemCubeSize * 0.5 : 0.01;
    const QcVec3 centre = axes.at(0.5, 0.5, QcHanging::frameDepth + lift);
    const double drop = look.cube ? 0.0 : QcEntityMesher::itemSpriteSize * 0.5;
    const QcEntityMesher::PlaceFunction place = [&](const QcVec3 &local) {
        return centre + itemRight * local.x + itemUp * (local.y - drop) + axes.out * local.z;
    };
    QcEntityMesher::appendPlacedItem(mesh, look, place, light, uv);
}

void appendPainting(QcEntityMesh &mesh, const QcClientEntity &entity, const QcHangingDefinition &definition, QcEntityLight light,
                    const QcEntityMesher::UvLookup &uv)
{
    const QcPaintingMotif &motif = definition.motifs[entity.hanging.motif];
    const Axes axes(entity.hanging);
    const QRectF back = uv(definition.back);
    const double depth = QcHanging::paintingDepth;

    // The edges in the back texture up to the picture, then one picture tile per block in front.
    boardEdges(mesh, axes, {0.0, 0.0, static_cast<double>(motif.width), static_cast<double>(motif.height), depth}, back, light);
    for (int row = 0; row < motif.height; ++row) {
        for (int column = 0; column < motif.width; ++column) {
            const QRectF tile = uv(motif.tileTexture(column, motif.height - 1 - row));
            const auto p = [&](double a, double b) { return axes.at(column + a, row + b, depth); };
            mesh.addQuad(quad(mesh, {p(0.0, 0.0), p(1.0, 0.0), p(1.0, 1.0), p(0.0, 1.0)}, tile, light, 1.0f));
        }
    }
}

} // namespace

namespace QcHangingMesher {

std::optional<quint32> framedMap(const QcClientEntity &entity, const QcContentRegistry &content)
{
    if (!entity.isHanging() || entity.hanging.type != QcHangingType::ItemFrame || entity.item.isEmpty() || entity.item.mapId() == 0
        || !content.isValidItem(entity.item.item) || content.item(entity.item.item).map != QcMapItem::Filled) {
        return std::nullopt;
    }
    return entity.item.mapId();
}

void appendFramedMap(QcEntityMesh &mesh, const QcHanging &hanging, const QRectF &uv, QcEntityLight light)
{
    const Axes axes(hanging);
    const auto p = [&](double a, double b) { return axes.at(a, b, QcHanging::frameDepth); };
    std::array<QcEntityVertex, 4> corners = quad(mesh, {p(0.0, 0.0), p(1.0, 0.0), p(1.0, 1.0), p(0.0, 1.0)}, uv, light, 1.0f);

    // Turning the picture clockwise moves each corner's texture coordinates one corner on.
    const std::array<QcEntityVertex, 4> unturned = corners;
    const auto turns = static_cast<std::size_t>(mapQuarterTurns(hanging.rotation));
    for (std::size_t corner = 0; corner < 4; ++corner) {
        corners[corner].u = unturned[(corner + turns) % 4].u;
        corners[corner].v = unturned[(corner + turns) % 4].v;
    }
    mesh.addQuad(corners);
}

void append(QcEntityMesh &mesh, const QcClientEntity &entity, const QcContentRegistry &content, QcEntityLight light,
            const QcEntityMesher::UvLookup &uv)
{
    // The spawn was checked on arrival (QcClientEntities): its own item hangs it and a painting's
    // motif is one of the item's.
    if (!entity.isHanging() || !content.isValidItem(entity.held)) {
        return;
    }
    const std::optional<QcHangingDefinition> &hangs = content.item(entity.held).hangs;
    if (!hangs) {
        return;
    }
    if (entity.hanging.type == QcHangingType::ItemFrame) {
        appendFrame(mesh, entity, content, light, uv);
    } else if (entity.hanging.motif < hangs->motifs.size()) {
        appendPainting(mesh, entity, *hangs, light, uv);
    }
}

} // namespace QcHangingMesher
