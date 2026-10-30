// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/showcase/QcShowcaseArea.h"

#include <QList>

/*! \brief The podiums of one block category (decision E-220): every state of every block of the
    category on a podium of its own, with a label naming the block and the state.

    Doors show both halves, blocks hanging on a side or the ceiling get the block they hang on,
    liquids stand in glass tanks and plants on their ground. Podiums stand in lines, at most a
    few dozen per line, and keep off the columns next to chunk borders: loading a neighbour chunk
    re-checks the blocks on a border (connections, support) and would change the states on show. */
namespace QcShowcaseBlockArea {

/*! Podiums per line at most. */
inline constexpr int maxLineSlots = 21;
/*! Blocks with more states show only those that differ from the default in one property. */
inline constexpr int maxAllStates = 32;

/*! Returns whether podiums may stand in the world column \a x: odd local x (in its chunk) up to
    13. */
bool usableX(int x);
/*! Returns whether podiums may stand in the line at world \a z: local z (in its chunk) 1, 4, 7,
    10 or 13. */
bool usableZ(int z);

/*! Returns the states of the block type \a type of \a content shown on podiums: all of them (up
    to maxAllStates), for doors only the lower halves. */
QList<QcBlockId> shownStates(const QcContentRegistry &content, quint32 type);

/*! Returns the area of \a category with the block types \a types (indices into the blocks of
    the content of \a context), built with its south west corner on the ground at \a anchor
    (QcShowcaseAreaBuilder). */
QcShowcaseArea build(const QcShowcaseContext &context, const QcShowcaseCategory &category, const QList<quint32> &types,
                     QcBlockPos anchor);

} // namespace QcShowcaseBlockArea
