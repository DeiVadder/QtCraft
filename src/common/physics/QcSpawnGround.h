// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"

/*! \brief Where creatures that walk may appear by themselves (decision E-836), the same for every
    kind instead of a block list per creature: the ground is a solid block whose top face is full
    and that is drawn opaque (no glass, leaves or ice, no lower slab, path or farmland; an upper
    slab and a double slab carry), and neither ground nor the cells of feet and head hurt or hold a
    body (QcBlockDefinition::keepsSpawnsAway).

    The cells of feet and head are free: not solid, no liquid, no rail. A spawn rule's
    \c {blocks} may still narrow the ground down (peaceful animals on grass). */
namespace QcSpawnGround {

/*! Returns whether a creature may appear standing on the block state \a ground of \a content. */
bool carries(const QcContentRegistry &content, QcBlockId ground);
/*! Returns whether a creature's feet or head may be in the block state \a cell of \a content when
    it appears. */
bool leavesRoom(const QcContentRegistry &content, QcBlockId cell);

/*! \brief How many states of a block carry spawns (the handbook's tables): all, some (slabs:
    only the upper and the double one) or none. */
enum class Carrying : quint8 {
    /*! Every state carries. */
    All,
    /*! Some states carry. */
    Some,
    /*! No state carries. */
    None
};
/*! Returns how many states of the block \a type (an index into QcContentRegistry::blocks()) of
    \a content carry spawns. */
Carrying carryingOf(const QcContentRegistry &content, quint32 type);

} // namespace QcSpawnGround
