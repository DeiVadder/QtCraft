// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/villages/QcVillagePlan.h"

#include <QList>

#include <optional>

class QcTerrainGenerator;

// The walkway of a piece with a water rule (decision E-404, QcVillageJetty), from the terrain noise alone like the
// rest of a village plan: from the end of a road straight on to the first water within reach, on the
// ground down to the shore and a few blocks out over the water just above it, with posts under its
// edges.
namespace QcJettyPlanner {

struct Request
{
    // The last road cell and the road's direction.
    int x = 0;
    int z = 0;
    int dx = 0;
    int dz = 0;
    // The road end's top block: the ground beyond it may step from there by one block per cell.
    int deckY = 0;
    // Water must begin at most this many blocks past the road end.
    int reach = 0;
    // Odd width and blocks out over the water.
    int width = 1;
    int over = 4;
    QcBlockId deck = qcAirBlockId;
    QcBlockId post = qcAirBlockId;
};

// Posts stand this far apart along the walkway over the water.
constexpr int postSpacing = 3;

// The walkway's cells; std::nullopt when no water lies within reach, the ground before it steps by
// more than a block or the water is narrower than two blocks.
[[nodiscard]] std::optional<QList<QcVillagePlan::Deck>> lay(const QcTerrainGenerator &terrain, const Request &request);

} // namespace QcJettyPlanner
