// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/villages/QcVillageDraft.h"

#include <QList>

#include <utility>

class QcTerrainGenerator;

/*! \brief The forecourt of a piece beside a diagonal road (decision E-408, E-419): its footprint
    behind its front row, and the side road cells from its doors straight out to the road. */
namespace QcVillageForecourt {

/*! Returns the footprint of a piece of \a size whose front row has its middle at \a middle, the
    door facing \a facing (an axis direction). */
QcVillageDraft::Rect behindFront(std::pair<int, int> middle, std::pair<int, int> facing, std::pair<int, int> size);
/*! Returns the forecourt cells of \a draft from the front of the piece with footprint \a rect
    (door facing \a facing) straight out to the road, three blocks wide in front of each of its
    \a doorsteps in the row before the front (else of the middle of the front), at most 12 cells
    out; columns beside a door that do not reach the road are left out. Empty when a door's own
    column does not reach the road or a column crosses a piece, water of \a terrain or a river
    delta. */
QList<std::pair<int, int>> forecourt(const QcTerrainGenerator &terrain, const QcVillageDraft &draft, QcVillageDraft::Rect rect,
                                     std::pair<int, int> facing, const QList<QcBlockPos> &doorsteps);

} // namespace QcVillageForecourt
