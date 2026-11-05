// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcPortalDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"

#include <QList>

#include <optional>

/*! \brief The inside of a portal frame (decision E-1021): its lowest inner cell, how it stands and
    its inner size. */
struct QcPortalFrame
{
    /*! \brief How the frame stands. */
    enum class Axis : quint8 {
        /*! Upright, its width along x. */
        X,
        /*! Upright, its width along z. */
        Z,
        /*! Lying, a ring: its width along x, its height along z. */
        Y,
    };

    /*! The inner cell with the lowest x, y and z. */
    QcBlockPos origin;
    /*! How the frame stands. */
    Axis axis = Axis::X;
    /*! The inner cells along the width: x, or z for Axis::Z. */
    int width = 0;
    /*! The inner cells along the height: y, or z for Axis::Y. */
    int height = 0;

    /*! Returns the inner cell \a across cells along the width and \a up cells along the height
        from the origin; a negative value or one at the size names the frame around it. */
    QcBlockPos at(int across, int up) const;
    /*! Returns the inner cells, row by row from the origin. */
    QList<QcBlockPos> cells() const;
    /*! Returns whether \a pos is an inner cell. */
    bool contains(QcBlockPos pos) const;
    /*! Returns where a traveller stands who arrives: the middle of the lowest inner row, on its
        floor (for a ring: the middle of the ring, one block above it). */
    QcVec3 arrival() const;
    /*! Returns the middle of the inside. */
    QcVec3 centre() const;
    bool operator==(const QcPortalFrame &) const = default;
};

/*! \brief Portal frames in the blocks of a world (decision E-1021): finding a complete frame around
    a cell, the block states that fill it, and the support rule of portal blocks (a portal block
    whose frame or neighbours go away goes out). */
namespace QcPortalShapes {

/*! \brief What the inside of a frame holds that find() accepts. */
enum class Inside : quint8 {
    /*! Air or fire: a frame that may light. */
    Empty,
    /*! The portal's own block: a lit portal. */
    Lit,
};

/*! Returns the complete frame of \a portal in \a world whose inside holds \a cell, every inner
    cell as \a inside says; for a vertical portal upright along x first, then along z. None when
    a frame block is missing or has no insert, a cell of the inside is something else, a
    neighbour is not loaded, or the size is outside the portal's limits. */
[[nodiscard]] std::optional<QcPortalFrame> find(const QcBlockAccess &world, const QcPortalDefinition &portal, QcBlockPos cell, Inside inside);
/*! Returns the complete empty ring of \a portal in \a world next to its frame block at
    \a frameBlock: the one whose inside holds one of the four horizontal neighbours. */
[[nodiscard]] std::optional<QcPortalFrame> ringBeside(const QcBlockAccess &world, const QcPortalDefinition &portal, QcBlockPos frameBlock);
/*! Returns the state of the block of \a portal in \a content that fills a frame standing as
    \a axis: with \c {axis} x or z when the block has that property. None when the content has
    no such block. */
[[nodiscard]] std::optional<QcBlockId> fillState(const QcContentRegistry &content, const QcPortalDefinition &portal, QcPortalFrame::Axis axis);
/*! Returns whether \a block of \a content is a frame block of \a portal; with \a needsInsert
    only with its insert. */
bool isFrame(const QcContentRegistry &content, const QcPortalDefinition &portal, QcBlockId block, bool needsInsert);
/*! Returns the first portal of \a content whose block \a block is (any state); null for none. */
const QcPortalDefinition *portalOfBlock(const QcContentRegistry &content, QcBlockId block);
/*! Returns whether the portal block \a id at \a pos of \a world still stands (the support
    rule "portal"): each neighbour in the plane of its frame (above, below and along its axis; for
    a ring the four horizontal ones) is the same portal block or a frame block of a portal it
    fills. Unloaded neighbours never break it, nor does a block of no portal. */
bool isHeld(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

} // namespace QcPortalShapes
