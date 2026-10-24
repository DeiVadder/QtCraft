#include "clientcore/meshing/QcEnchantingMesher.h"

#include "clientcore/textures/QcRuneGlyphs.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

// A half of the book: from the spine to its edge (metres) and along the spine (the tile is square).
constexpr double halfWidth = 0.3;
constexpr double pageHeight = 2.0 * halfWidth;
// The book leans back a little, as if lying on an invisible lectern.
constexpr double lean = 0.35;

} // namespace

namespace QcEnchantingMesher {

void appendBook(QcEntityMesh &mesh, const QcEnchantingAura::Book &book, QcEntityLight light, const QcEntityMesher::UvLookup &uv)
{
    const QRectF rect = uv(QString::fromLatin1(QcRuneGlyphs::bookTileName));
    const QcVec3 facing{std::sin(book.yaw), 0.0, std::cos(book.yaw)};
    const QcVec3 right{facing.z, 0.0, -facing.x};
    const QcVec3 up = QcVec3{0.0, std::cos(lean), 0.0} - facing * std::sin(lean);
    const QcVec3 bottom = book.spine - up * (pageHeight * 0.5);
    const QcVec3 top = book.spine + up * (pageHeight * 0.5);
    // Wide open the halves lie nearly flat towards the viewer; closed they meet behind the spine.
    const double fold = qDegreesToRadians(10.0 + 80.0 * (1.0 - book.open));
    for (const double side : {-1.0, 1.0}) {
        const QcVec3 edge = (right * (side * std::cos(fold)) - facing * std::sin(fold)) * halfWidth;
        const auto u = static_cast<float>(side < 0.0 ? rect.left() : rect.right());
        const auto middle = static_cast<float>(rect.center().x());
        const std::array<QcVec3, 4> corners{bottom, bottom + edge, top + edge, top};
        const std::array<float, 4> us{middle, u, u, middle};
        std::array<QcEntityVertex, 4> quad;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            QcEntityVertex vertex = mesh.vertexAt(corners[corner]);
            vertex.u = us[corner];
            vertex.v = static_cast<float>(corner < 2 ? rect.bottom() : rect.top());
            vertex.skyLight = light.sky;
            vertex.blockLight = light.block;
            quad[corner] = vertex;
        }
        mesh.addQuad(quad, true);
    }
}

void appendRune(QcEntityMesh &mesh, const QcEnchantingAura::Rune &rune, const QcVec3 &viewer, const QcEntityMesher::UvLookup &uv)
{
    const QRectF page = uv(QcRuneGlyphs::pageName(QcRuneGlyphs::pageOf(rune.rune)));
    const int cell = QcRuneGlyphs::cellOf(rune.rune);
    // The cell's column and row in the page.
    const int column = cell % QcRuneGlyphs::cellsPerRow;
    const int row = cell / QcRuneGlyphs::cellsPerRow;
    const double cellWidth = page.width() / QcRuneGlyphs::cellsPerRow;
    const double cellHeight = page.height() / QcRuneGlyphs::cellsPerRow;
    const QRectF rect(page.left() + column * cellWidth, page.top() + row * cellHeight,
                      cellWidth, cellHeight);

    // Facing the viewer about the vertical.
    const double dx = viewer.x - rune.position.x;
    const double dz = viewer.z - rune.position.z;
    const double length = std::max(1e-6, std::sqrt(dx * dx + dz * dz));
    const QcVec3 right{dz / length * rune.size * 0.5, 0.0, -dx / length * rune.size * 0.5};
    const QcVec3 up{0.0, rune.size * 0.5, 0.0};
    const std::array<QcVec3, 4> corners{rune.position - right - up, rune.position + right - up, rune.position + right + up,
                                        rune.position - right + up};
    std::array<QcEntityVertex, 4> quad;
    for (std::size_t corner = 0; corner < 4; ++corner) {
        const bool left = corner == 0 || corner == 3;
        QcEntityVertex vertex = mesh.vertexAt(corners[corner]);
        vertex.u = static_cast<float>(left ? rect.left() : rect.right());
        vertex.v = static_cast<float>(corner < 2 ? rect.bottom() : rect.top());
        vertex.blockLight = 1.0f;
        vertex.tint = runeTint;
        quad[corner] = vertex;
    }
    mesh.addQuad(quad, true);
}

} // namespace QcEnchantingMesher
