// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcMeshOutput.h"

#include <utility>

namespace {

// One side of one diagonal quad of a cross, running from ends[0] to ends[1], in the cell whose
// corner lies at `local` in the section.
std::array<QcBlockVertex, 4> crossSide(const QcBlockRenderTable::Cross &cross, std::array<std::array<float, 2>, 2> ends,
                                       QcBlockPos local, QcQuadLight light)
{
    const QRectF &uv = cross.uv;
    const float y0 = cross.box[1];
    const float y1 = cross.box[4];

    std::array<QcBlockVertex, 4> corners{};
    for (std::size_t corner = 0; corner < 4; ++corner) {
        const auto &end = ends[(corner == 1 || corner == 2) ? 1 : 0];
        QcBlockVertex &vertex = corners[corner];
        vertex.x = static_cast<float>(local.x) + end[0];
        vertex.y = static_cast<float>(local.y) + (corner < 2 ? y0 : y1);
        vertex.z = static_cast<float>(local.z) + end[1];
        vertex.u = static_cast<float>((corner == 0 || corner == 3) ? uv.left() : uv.right());
        vertex.v = static_cast<float>(corner < 2 ? uv.bottom() : uv.top());
        vertex.setShade(light.shade);
        vertex.setSkyLight(light.sky);
        vertex.setBlockLight(light.block);
    }

    return corners;
}

} // namespace

void QcMeshOutput::appendQuad(const std::array<QcBlockVertex, 4> &corners, bool flipDiagonal, QcQuadSurface surface) const
{
    const auto base = static_cast<quint32>(vertices.size());
    for (QcBlockVertex vertex : corners) {
        vertex.setSurface(surface.normal, surface.material);
        vertices.append(vertex);
    }
    if (flipDiagonal) {
        indices.append({base + 1, base + 2, base + 3, base + 1, base + 3, base});
    } else {
        indices.append({base, base + 1, base + 2, base, base + 2, base + 3});
    }
    if (runs) {
        noteRun();
    }
}

void QcMeshOutput::appendCross(const QcBlockRenderTable::Cross &cross, QcBlockPos local, QcQuadLight light,
                               QcSurfaceMaterial material) const
{
    const bool plant = material == QcSurfaceMaterial::Solid || material == QcSurfaceMaterial::Cutout;
    const QcQuadSurface surface{std::nullopt, plant ? QcSurfaceMaterial::Plant : material};
    const auto &[x0, y0, z0, x1, y1, z1] = cross.box;
    const std::array<std::array<float, 4>, 2> diagonals = {{{x0, z0, x1, z1}, {x0, z1, x1, z0}}};
    for (const auto &[ax, az, bx, bz] : diagonals) {
        for (const bool back : {false, true}) {
            const std::array<std::array<float, 2>, 2> ends = back ? std::array<std::array<float, 2>, 2>{{{bx, bz}, {ax, az}}}
                                                                   : std::array<std::array<float, 2>, 2>{{{ax, az}, {bx, bz}}};
            appendQuad(crossSide(cross, ends, local, light), false, surface);
        }
    }
}

// Six more indices of the kind `run` in the runs: the last run grows or a new one starts (the
// first run is water, possibly empty).
void QcMeshOutput::noteRun() const
{
    if (runs->isEmpty() && run == Run::OtherTranslucent) {
        runs->append(0);
    }
    if (!runs->isEmpty() && (runs->size() - 1) % 2 == std::to_underlying(run)) {
        runs->back() += 6;
    } else {
        runs->append(6);
    }
}
