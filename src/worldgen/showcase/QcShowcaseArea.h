// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "worldgen/structures/QcStructure.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

struct QcVillagePlan;

/*! \brief What every area of the showcase world is built from: the content, the settings and the
    blocks they name, resolved once (decision E-220). */
struct QcShowcaseContext
{
    /*! The content the areas show. */
    const QcContentRegistry &content;
    /*! The showcase settings of the content. */
    const QcShowcaseDefinition &settings;
    /*! Height of the ground's top layer; areas stand on it. */
    int groundTop = 0;
    /*! The block of the ground's top layer. */
    QcBlockId ground = qcAirBlockId;
    /*! The block of the podiums (settings \c {podium}, else stone). */
    QcBlockId podium = qcAirBlockId;
    /*! The block of the enclosures (settings \c {enclosure}, else glass). */
    QcBlockId enclosure = qcAirBlockId;
    /*! The block of the containers (settings \c {container}, else a chest). */
    QcBlockId container = qcAirBlockId;
};

/*! \brief One area of the showcase: a structure (local y 0 is the ground layer) and where it
    goes. */
struct QcShowcaseArea
{
    /*! Short name for tour screenshots ("blocks-stone", "creatures"). */
    QString name;
    /*! Translation key of the area's title. */
    QString title;
    /*! The structure the area is built as. */
    std::shared_ptr<const QcStructure> structure;
    /*! Where the structure's local 0, 0, 0 lies in the world (its lowest north west corner;
        placed unturned). */
    QcBlockPos origin;
    /*! The villages it shows, in world coordinates (the village area's rows, decision E-411). */
    QList<std::shared_ptr<const QcVillagePlan>> villages;
    /*! Beds (heads) and workstations outside its villages that count as placed (decision E-490,
        QcWorldGenerator::pointsOfInterestIn), in world coordinates. */
    QList<QcBlockPos> pointsOfInterest;
    /*! Its tour stop looks from in front of its south side
        (QcStructureDefinition::showcaseFromFront). */
    bool fromFront = false;
};

/*! Builds an area whose south west corner on the ground is \c {anchor} (x: west edge, y: ground
    top, z: south edge); the area grows east and north from there. */
using QcShowcaseAreaBuilder = std::function<QcShowcaseArea(QcBlockPos anchor)>;
