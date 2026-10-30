// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockFace.h"
#include "common/light/QcLightCells.h"
#include "common/light/QcLightNibbles.h"
#include "common/light/QcLightTable.h"
#include "common/world/QcChunk.h"

#include <algorithm>
#include <array>
#include <memory>

/*! \brief The light of the open sky in a chunk (for QcLightEngine): the top of each cell column
    (the highest block that stops sky light), the cells above the tops that the open sky lights,
    and the cells beside a higher neighbouring column from which the light spreads sideways. */
namespace QcSkyLight {

/*! The top of every cell column of a chunk, by QcLightCells::columnIndex; -1 for a column open
    down to the bottom of the world. */
using Tops = std::array<qint16, static_cast<std::size_t>(QcWorld::chunkSize) * QcWorld::chunkSize>;

/*! Returns the top of the cell column (\a lx, \a lz) of \a blocks, with the light properties of
    \a table; -1 for a column open down to the bottom. */
[[nodiscard]] int columnTop(const QcChunk &blocks, const QcLightTable &table, int lx, int lz);
/*! Returns the tops of all cell columns of \a blocks, with the light properties of \a table. */
[[nodiscard]] Tops tops(const QcChunk &blocks, const QcLightTable &table);
/*! Returns the sky light of the section from \a low to \a high that \a tops cut: full above
    them, dark below. */
[[nodiscard]] std::unique_ptr<QcLightNibbles> sectionCells(const Tops &tops, int low, int high);

/*! Calls \c {visit(lx, y, lz)} (\a visit) for every cell of the chunk above the top of its
    column in \a tops and up to the highest top of its four neighbour columns: those cells border
    darker cells and spread the light. \c {topAt(lx, lz)} (\a topAt) is the top of a column of
    this chunk or, outside 0..chunkSize - 1, of a neighbour chunk (-1 when it is not loaded). */
template<typename TopAt, typename Visit>
void forEachEdgeCell(const Tops &tops, TopAt topAt, Visit visit)
{
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            const int top = tops[static_cast<std::size_t>(QcLightCells::columnIndex(lx, lz))];
            int reach = top + 1;
            for (const QcBlockFace face : QcLightCells::horizontalFaces) {
                const QcFaceOffset offset = qcFaceOffset(face);
                reach = std::max(reach, topAt(lx + offset.dx, lz + offset.dz));
            }
            for (int y = top + 1; y <= std::min(reach, QcWorld::worldHeight - 1); ++y) {
                visit(lx, y, lz);
            }
        }
    }
}

} // namespace QcSkyLight
