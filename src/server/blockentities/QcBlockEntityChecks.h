// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>

class QcServerWorld;

/*! \brief The rules between the blocks of a world and its block entities, checked in one place
    (decision E-357; tests call it after their scenarios, a broken rule is a bug).

    The rules:

    \list
    \li a container exists exactly when its block has \c {container}, with the block's number of
        slots (missing only while the table of containers is full); likewise a machine for
        \c {machine};
    \li a sapling timer only on a sapling block; a hopper cooldown only in a hopper's container;
    \li what a component remembers fits its kind (burnout: torch, powered: opener, extension:
        piston);
    \li the counts, the order of saplings and the chunks of scheduled ticks agree with the entries;
    \li every burning fire of the fire queue lies on a fire block of a loaded chunk, within the caps
        per chunk and world (decision E-392).
    \endlist
    Only loaded chunks are compared with their blocks. */
namespace QcBlockEntityChecks {

/*! Returns every rule \a world breaks as a line naming the position; empty when all hold. */
QStringList problems(const QcServerWorld &world);

} // namespace QcBlockEntityChecks
