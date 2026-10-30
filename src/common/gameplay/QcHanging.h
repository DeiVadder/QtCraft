// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blockbehaviour/QcHangingDefinition.h"
#include "common/core/QcAabb.h"
#include "common/core/QcBlockFace.h"
#include "common/core/QcBlockPos.h"

#include <QList>

/*! \brief Where an item frame or a painting hangs and what it shows (decisions E-790, E-791), the
    same for the server (placement, support, saving) and the client (targeting, drawing).

    It lies flat on \c {face} of the blocks behind its cells: a frame in one cell on any face, a
    painting of width x height cells on a side face. */
struct QcHanging
{
    /*! Steps an item in a frame turns through (45 degrees each). */
    static constexpr int rotations = 8;
    /*! How far a frame or painting stands out from its wall. \qcshared */
    static constexpr double frameDepth = 1.0 / 16.0;
    static constexpr double paintingDepth = 1.0 / 16.0;
    /*! A frame covers this much of its face (centred). */
    static constexpr double frameSide = 12.0 / 16.0;

    /*! Item frame or painting. */
    QcHangingType type = QcHangingType::ItemFrame;
    /*! The face of the supporting blocks it lies on, pointing away from them into its cells. */
    QcBlockFace face = QcBlockFace::North;
    /*! A frame's cell; a painting's lower left cell as seen from the front. */
    QcBlockPos cell;
    /*! A painting's size in blocks; 1 x 1 for a frame. \qcshared */
    int width = 1;
    int height = 1;
    /*! Item frames: the eighths the held item is turned (0 to rotations - 1). */
    quint8 rotation = 0;
    /*! Paintings: index into the motifs of its item. */
    quint8 motif = 0;

    /*! Returns the cells it covers. */
    QList<QcBlockPos> cells() const;
    /*! Returns the block behind \a covered (one of its cells) that holds it there. */
    QcBlockPos supportOf(QcBlockPos covered) const;
    /*! Its thin box on the wall (targeting, hits). */
    QcAabb box() const;
    /*! Returns the middle of its front, the entity's position. */
    QcVec3 centre() const;

    /*! Returns the direction to the right as seen from the front of the side face \a face (the
        cells of a painting run this way from \c {cell}); for Up and Down the direction a frame's
        item points at rotation 0. */
    static QcFaceOffset rightOf(QcBlockFace face);
    /*! Returns whether a painting may hang on \a face (the four side faces). */
    static bool isWallFace(QcBlockFace face) { return face != QcBlockFace::Up && face != QcBlockFace::Down; }

    bool operator==(const QcHanging &) const = default;
};
