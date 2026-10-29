// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcStructureDefinition.h"
#include "common/content/worldgen/QcWorldTypeDefinitions.h"

#include <QList>
#include <QString>

#include <optional>

// What a world generator puts into a new world once, when the world is created (decision E-221):
// the world's rules and what its structures bring along, in world coordinates: creatures, contents
// of chests and machines, minecarts, boats, block updates to run (liquids flow, sand falls) and saplings that grow.
// The server applies it before the first player joins and records that in world.json.
struct QcWorldSetup
{
    using Mob = QcStructureDefinition::Mob;
    using Stand = QcStructureDefinition::Stand;
    using Contents = QcStructureDefinition::Contents;
    using Cart = QcStructureDefinition::Cart;
    using Boat = QcStructureDefinition::Boat;
    using Sign = QcStructureDefinition::Sign;
    using Hanging = QcStructureDefinition::Hanging;

    // Seconds into the day the world starts at (QcDayCycle), else the usual morning.
    std::optional<double> timeOfDay;
    bool daylightCycle = true;
    bool mobSpawning = true;
    bool randomTicks = true;
    // Game rule fireSpread (decision E-392).
    bool fireSpread = true;
    // Game rule weatherCycle (decision E-395).
    bool weatherCycle = true;
    // Game rule mobGriefing (decision E-460).
    bool mobGriefing = true;
    // Game rule explosionBlockDamage (decision E-603).
    bool explosionBlockDamage = true;
    QList<Mob> mobs;
    QList<Stand> stands;
    QList<Contents> contents;
    QList<QcBlockPos> updates;
    QList<QcBlockPos> growing;
    // Minecarts on rails (decisions E-382, E-386).
    QList<Cart> minecarts;
    // Boats (decision E-920).
    QList<Boat> boats;
    // Text on signs (decision E-766).
    QList<Sign> signs;
    // Item frames and paintings (decision E-797).
    QList<Hanging> hangings;
    // The adventure allow-lists the world starts with (decision E-735; the showcase's station).
    QcAdventureRules adventure;

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
