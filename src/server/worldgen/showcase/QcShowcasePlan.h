#pragma once

#include "QcStructurePlacement.h"
#include "QcWorldLabel.h"
#include "QcWorldSetup.h"

#include <QHash>
#include <QList>
#include <QString>

// A fixed camera position of the screenshot tour through the showcase world: the player stands on
// top of the column (x, z) and looks along yaw and pitch (degrees, QcCamera conventions).
struct QcShowcaseTourStop
{
    QString name;
    int x = 0;
    int z = 0;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

// Where an area of the showcase lies, for tests and the tour.
struct QcShowcaseAreaInfo
{
    QString name;
    QString title;
    QcBlockPos origin;
    int sizeX = 0;
    int sizeY = 0;
    int sizeZ = 0;
};

// The finished layout of the showcase world (decision E-220): the flat ground, every structure
// placement with an index by chunk, the labels by chunk, the one-time setup and the tour. Built once
// per content (QcShowcasePlanner), immutable afterwards and read by the generator's threads.
struct QcShowcasePlan
{
    // Block per height of the flat ground, from y 0 to the top layer.
    QList<QcBlockId> groundColumn;
    QList<QcStructurePlacement> placements;
    QHash<QcChunkPos, QList<qsizetype>> placementsByChunk;
    QHash<QcChunkPos, QList<QcWorldLabel>> labelsByChunk;
    QList<QcShowcaseAreaInfo> areas;
    QcWorldSetup setup;
    QcBlockPos spawn;
    QList<QcShowcaseTourStop> tour;

    int groundTop() const { return int(groundColumn.size()) - 1; }
    qsizetype labelCount() const;
};
