// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcWorldLabel.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/generators/QcStructureSites.h"
#include "worldgen/generators/QcWorldSetup.h"
#include "worldgen/structures/QcStructurePlacement.h"

#include <QHash>
#include <QList>
#include <QString>

#include <memory>

struct QcVillagePlan;

/*! \brief A fixed camera position of the screenshot tour through the showcase world: the player
    stands on top of the column (x, z) and looks along yaw and pitch (degrees, QcCamera
    conventions). */
struct QcShowcaseTourStop
{
    /*! The stop's name (the area's name, with \c {-near} for the near stop). */
    QString name;
    /*! The x, z of the column the player stands on. \qcshared */
    int x = 0;
    int z = 0;
    /*! The yaw to look along in degrees. */
    float yaw = 0.0f;
    /*! The pitch to look along in degrees. */
    float pitch = 0.0f;
};

/*! \brief Where an area of the showcase lies, for tests and the tour. */
struct QcShowcaseAreaInfo
{
    /*! The area's short name (QcShowcaseArea::name). */
    QString name;
    /*! Translation key of the area's title. */
    QString title;
    /*! Where the area's structure has its local 0, 0, 0 in the world. */
    QcBlockPos origin;
    /*! The size of the area's structure along x, y and z. \qcshared */
    int sizeX = 0;
    int sizeY = 0;
    int sizeZ = 0;
};

/*! \brief The finished layout of the showcase world (decision E-220): the flat ground, every
    structure placement with an index by chunk, the labels by chunk, the one-time setup and the
    tour.

    Built once per content (QcShowcasePlanner), immutable afterwards and read by the generator's
    threads. */
struct QcShowcasePlan
{
    /*! Block per height of the flat ground, from y 0 to the top layer. */
    QList<QcBlockId> groundColumn;
    /*! Every structure placement. */
    QList<QcStructurePlacement> placements;
    /*! Indices into placements of the placements that reach into each chunk. */
    QHash<QcChunkPos, QList<qsizetype>> placementsByChunk;
    /*! The labels by chunk. */
    QHash<QcChunkPos, QList<QcWorldLabel>> labelsByChunk;
    /*! Where each area lies. */
    QList<QcShowcaseAreaInfo> areas;
    /*! What the world gets once when it is created. */
    QcWorldSetup setup;
    /*! The structures' spawners and loot chests by chunk (decision E-520). */
    QHash<QcChunkPos, QcChunkFurnishing> furnishingByChunk;
    /*! The structures' guardian zones (decision E-591) and spawn areas (decision E-871). */
    QcStructureSites sites;
    /*! The spawn point: on the ground at the origin. */
    QcBlockPos spawn;
    /*! The stops of the screenshot tour, in order. */
    QList<QcShowcaseTourStop> tour;
    /*! The rows of the village area, each a village of its own (decision E-411). */
    QList<std::shared_ptr<const QcVillagePlan>> villages;
    /*! The areas' points of interest (QcShowcaseArea::pointsOfInterest) by chunk, in their
        order. */
    QHash<QcChunkPos, QList<QcBlockPos>> pointsOfInterestByChunk;

    /*! Returns the height of the ground's top layer. */
    int groundTop() const { return static_cast<int>(groundColumn.size()) - 1; }
    /*! Returns the number of labels in all chunks. */
    qsizetype labelCount() const;
};
