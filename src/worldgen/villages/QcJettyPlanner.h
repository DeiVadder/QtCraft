// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/villages/QcVillagePlan.h"

#include <QList>

#include <optional>

class QcTerrainGenerator;

/*! \brief The walkway of a piece with a water rule (decision E-404, QcVillageJetty), from the
    terrain noise alone like the rest of a village plan: from the end of a road straight on to
    the first water within reach, on the ground down to the shore and a few blocks out over the
    water just above it, with posts under its edges. */
namespace QcJettyPlanner {

/*! \brief Where and how to lay a walkway (lay()). */
struct Request
{
    /*! The x and z of the last road cell. \qcshared */
    int x = 0;
    int z = 0;
    /*! The road's direction; the walkway continues in it. \qcshared */
    int dx = 0;
    int dz = 0;
    /*! The road end's top block: the ground beyond it may step from there by one block per cell. */
    int deckY = 0;
    /*! Water must begin at most this many blocks past the road end. */
    int reach = 0;
    /*! The walkway's odd width, centred on the road's middle line. */
    int width = 1;
    /*! At most this many cells of the walkway lie over the water (fewer where the water ends). */
    int over = 4;
    /*! The block of the walkway's cells. */
    QcBlockId deck = qcAirBlockId;
    /*! The block of the posts under the walkway's edges. */
    QcBlockId post = qcAirBlockId;
};

/*! Posts stand this far apart along the walkway over the water. */
constexpr int postSpacing = 3;

/*! Returns the walkway's cells for \a request over \a terrain; std::nullopt when no water lies
    within reach, the ground before it steps by more than a block or is an overhang, the water is
    narrower than two blocks or the walkway touches a river delta (decision E-850). */
[[nodiscard]] std::optional<QList<QcVillagePlan::Deck>> lay(const QcTerrainGenerator &terrain, const Request &request);

} // namespace QcJettyPlanner
