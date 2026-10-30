// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/terrain/QcTreeShape.h"

#include <QList>
#include <QSet>

#include <utility>

/*! \brief The blocks of one tree while QcTreeShape builds it: passes them on, and for trees with
    vines remembers what the tree occupies and which of its blocks vines may hang on, so vines go
    only to free sides.

    Holds the giant crowns on a 2×2 trunk (decision E-822). */
class QcTreeLayout
{
public:
    /*! Giant crowns reach this far beyond their 2×2 trunk, no further than leaves stay without a
        log (QcPlantManager::leafReach). */
    static constexpr int giantCrownRadius = 4;

    QcTreeLayout(const QcTreeShape::Params &params, const QcTreeShape::Put &put);

    /*! Places \a block at \a pos as a trunk or branch block; \a holdsVines lets vines hang on its
        free sides. */
    void log(QcBlockPos pos, QcBlockId block, bool holdsVines);
    /*! Places the leaves \a block at \a pos, only where QcTreeShape::mayReplace() allows;
        \a holdsVines lets vines hang on its free sides, and vines hang only on leaves that were
        placed. */
    void leaf(QcBlockPos pos, QcBlockId block, bool holdsVines);
    /*! \brief How the corners of a square layer of leaves are thinned: each left out with
        \c {dropChance}. */
    struct Corners
    {
        /*! The seed of the random value per corner. */
        quint64 seed = 0;
        /*! The chance that a corner is left out. */
        double dropChance = 0.0;
    };
    /*! Places a square layer of \a leaves \a radius blocks around \a centre, its corners
        thinned as \a corners says. */
    void squareLayer(QcBlockPos centre, int radius, QcBlockId leaves, Corners corners);
    /*! Hangs the vines of \a params on free sides of the blocks that hold them, by chance from
        \a seed; nothing for trees without vines. */
    void hangVines(const QcTreeShape::Params &params, quint64 seed) const;

    /*! Builds the crown of a tall spruce with \a params on the 2×2 trunk at \a base of
        \a trunkHeight blocks: layered rings over most of the trunk, widest at the bottom, ragged
        by \a seed. */
    void buildGiantCone(const QcTreeShape::Params &params, QcBlockPos base, int trunkHeight, quint64 seed);
    /*! Builds the branches and crown of a jungle giant with \a params on the 2×2 trunk at
        \a base of \a trunkHeight blocks: short branches with leaves along the upper trunk and a
        wide flat crown, placed by \a seed. */
    void buildGiantRound(const QcTreeShape::Params &params, QcBlockPos base, int trunkHeight, quint64 seed);

private:
    void remember(QcBlockPos pos, double vineChance);
    // One layer of a giant crown: the cells within `radius` of the 2×2 trunk at `base` (its y is the
    // layer's), the outer ring thinned by chance.
    void giantLayer(const QcTreeShape::Params &params, QcBlockPos base, int radius, quint64 seed);
    // A branch leaving the trunk at `start` (a trunk block) towards side `side`, with leaves at its end.
    void branch(const QcTreeShape::Params &params, QcBlockPos start, int side, quint64 seed);

    const QcTreeShape::Put &m_put;
    bool m_tracks = false;
    QSet<QcBlockPos> m_occupied;
    // Blocks vines may hang on, with the chance per free side.
    QList<std::pair<QcBlockPos, double>> m_holders;
    // Whether the blocks placed next hold vines (set by the giant builders for their lowest layer).
    double m_leafVineChance = 0.0;
};
