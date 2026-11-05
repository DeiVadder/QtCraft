// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"
#include "common/world/QcEntityKind.h"

class QcServerDimension;

/*! \brief Moving a creature, an item or a minecart from one dimension to another (decision
    E-1024), the way a portal carries them.

    Each keeps its entity id (ids are unique across the dimensions, QcDimensions) and what it is
    saved with: a creature goes as its saved record (QcMobManager::storedOf) and leaves its
    dimension as if its chunk unloaded, an item keeps its stack, age and pickup delay, a minecart
    its contents (a rider stays behind). The chunk at the destination must be loaded. */
namespace QcEntityTravel {

/*! \brief Which entity travels from where to where. \qcfields */
struct Journey
{
    QcEntityKind kind = QcEntityKind::Item;
    quint32 id = 0;
    QcServerDimension *from = nullptr;
    QcServerDimension *to = nullptr;
    QcVec3 position;
};

/*! Moves the entity of \a journey; returns false when it is no creature, item or minecart of
    \c {from}, or \c {to} has no room for it (a minecart beyond the limit stays). */
bool move(const Journey &journey);

} // namespace QcEntityTravel
