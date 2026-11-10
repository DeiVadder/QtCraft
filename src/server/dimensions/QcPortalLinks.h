// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/blockrules/QcPortalShapes.h"
#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/content/worldgen/QcPortalDefinition.h"

#include <optional>

class QcServerBlocks;
class QcServerDimension;
class QcServerState;

/*! \brief Where a portal leads (decision E-1025): the dimension and the point a traveller arrives
    at, and the portal or platform made there for him.

    Used in its \c {from}, a portal leads to \c {to}: by a scaled link to a known portal of the
    same kind within its search radius around the scaled point (QcPortalIndex), else to a new one
    on ground within its create radius, else to a new one where it must stand, on a platform; by a
    fixed link to the arrival point of \c {to} (QcDimensionDefinition::arrival) with its platform,
    and no portal back. Used in \c {to}, it leads back to \c {from} by the scaled link, or to the
    world spawn. What it builds it builds through QcServerBlocks in the destination. */
class QcPortalLinks
{
public:
    /*! \brief A dimension and a point in it. \qcfields */
    struct Destination
    {
        QcServerDimension *dimension = nullptr;
        QcVec3 position;
    };

    QcPortalLinks(QcServerState &state, QcServerBlocks &blocks);

    /*! Returns where a traveller at \a position goes through the lit \a portal of the current
        dimension, opening the dimension there and building what the link needs; none when the
        portal does not lead anywhere from here or its destination cannot open. */
    [[nodiscard]] std::optional<Destination> destination(const QcPortalDefinition &portal, const QcVec3 &position);
    /*! Lights \a frame of \a portal in the current dimension: fills it with the portal's block and
        records it (QcPortalIndex). Returns whether it was lit. */
    bool light(const QcPortalDefinition &portal, const QcPortalFrame &frame);

private:
    // The scaled link into `target` from `position` of the current dimension.
    QcVec3 scaled(const QcPortalDefinition &portal, QcServerDimension &target, const QcVec3 &position);
    // A known portal near `point` in the current dimension, forgetting those that went out.
    std::optional<QcPortalFrame> known(const QcPortalDefinition &portal, const QcVec3 &point, double radius);
    // A new portal near `point`: on ground within the create radius, else on a platform at the point.
    QcPortalFrame create(const QcPortalDefinition &portal, const QcVec3 &point);
    // The fixed `arrival` of `target`, its platform made.
    QcVec3 fixed(QcServerDimension &target, const QcDimensionDefinition::Arrival &arrival);
    // The `platform` block at `floor` (none: left as it is) and air in the three cells above.
    void makeRoom(QcBlockPos floor, std::optional<QcBlockId> platform);
    // Loads the chunks within `radius` blocks of `point` in the current dimension.
    void loadAround(const QcVec3 &point, int radius);

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
};
