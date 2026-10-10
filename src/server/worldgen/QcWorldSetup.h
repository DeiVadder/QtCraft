#pragma once

#include "QcStructureDefinition.h"
#include "QcWorldTypeDefinitions.h"

#include <QList>
#include <QString>

#include <optional>

// What a world generator puts into a new world once, when the world is created (decision E-221):
// the world's rules and what its structures bring along, in world coordinates: creatures, contents
// of chests and machines, block updates to run (liquids flow, sand falls) and saplings that grow.
// The server applies it before the first player joins and records that in world.json.
struct QcWorldSetup
{
    using Mob = QcStructureDefinition::Mob;
    using Contents = QcStructureDefinition::Contents;

    // Seconds into the day the world starts at (QcDayCycle), else the usual morning.
    std::optional<double> timeOfDay;
    bool daylightCycle = true;
    bool mobSpawning = true;
    QList<Mob> mobs;
    QList<Contents> contents;
    QList<QcBlockPos> updates;
    QList<QcBlockPos> growing;

    // Only the rules of a world type (flat, void).
    static QcWorldSetup fromRules(const QcWorldTypeRules &rules)
    {
        QcWorldSetup setup;
        setup.timeOfDay = rules.timeOfDay;
        setup.daylightCycle = rules.daylightCycle;
        setup.mobSpawning = rules.mobSpawning;
        return setup;
    }
};
