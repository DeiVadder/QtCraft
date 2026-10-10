#pragma once

#include "QcContentRegistry.h"
#include "QcStructure.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

// What every area of the showcase world is built from: the content, the settings and the blocks
// they name, resolved once (decision E-220).
struct QcShowcaseContext
{
    const QcContentRegistry &content;
    const QcShowcaseDefinition &settings;
    // Height of the ground's top layer; areas stand on it.
    int groundTop = 0;
    QcBlockId ground = qcAirBlockId;
    QcBlockId podium = qcAirBlockId;
    QcBlockId enclosure = qcAirBlockId;
    QcBlockId container = qcAirBlockId;
};

// One area of the showcase: a structure (local y 0 is the ground layer) and where it goes.
struct QcShowcaseArea
{
    // Short name for tour screenshots ("blocks-stone", "creatures").
    QString name;
    // Translation key of the area's title.
    QString title;
    std::shared_ptr<const QcStructure> structure;
    QcBlockPos origin;
};

// Builds an area whose south west corner on the ground is `anchor` (x: west edge, y: ground top,
// z: south edge); the area grows east and north from there.
using QcShowcaseAreaBuilder = std::function<QcShowcaseArea(QcBlockPos anchor)>;
