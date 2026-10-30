// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QList>

#include <optional>

class QcContentRegistry;

/*! \brief Maps in the crafting grid (decision E-804), besides their recipes: a filled map with
    empty maps of its kind and nothing else copies it (one copy per empty map, all with its map
    id); a filled map in the middle of the 3 x 3 grid with its \c {zoomsWith} item in the eight
    cells around it zooms it out (a new map one scale larger, which the server makes when the
    result is taken). */
namespace QcMapCrafting {

/*! \brief What the grid makes. */
struct Result
{
    /*! The kinds of map crafting. */
    enum class Kind : quint8 {
        /*! Copies a filled map. */
        Copy,
        /*! Zooms a filled map out. */
        Zoom
    };
    /*! What the grid does. */
    Kind kind = Kind::Copy;
    /*! Copy: the copies with the original (count) and the map id. Zoom: one filled map with the
        id of the map to zoom out. */
    QcItemStack stack;
};

/*! Returns what the cells \a grid (row by row, \a gridSize wide; items of \a content) make, or
    nothing when they are no map crafting. */
[[nodiscard]] std::optional<Result> result(const QcContentRegistry &content, const QList<QcItemStack> &grid, int gridSize);

} // namespace QcMapCrafting
