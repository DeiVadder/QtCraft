// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/villages/QcVillageDraft.h"

#include <QList>

#include <utility>

class QcTerrainGenerator;

// The forecourt of a piece beside a diagonal road (decision E-408, E-419): its footprint behind
// its front row, and the side road cells from its doors straight out to the road.
namespace QcVillageForecourt {

// The footprint of a piece of `size` whose front row has its middle at `middle`, the door facing
// `facing` (an axis direction).
QcVillageDraft::Rect behindFront(std::pair<int, int> middle, std::pair<int, int> facing, std::pair<int, int> size);
// The forecourt cells from the front of a piece (door facing `facing`) to the road, three blocks
// wide in front of each of its `doorsteps` (else of the middle of the front); empty when one of
// them does not reach the road or the forecourt crosses a piece.
QList<std::pair<int, int>> forecourt(const QcTerrainGenerator &terrain, const QcVillageDraft &draft, QcVillageDraft::Rect rect,
                                     std::pair<int, int> facing, const QList<QcBlockPos> &doorsteps);

} // namespace QcVillageForecourt
