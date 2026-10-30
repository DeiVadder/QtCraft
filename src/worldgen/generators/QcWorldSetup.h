// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcStructureDefinition.h"
#include "common/content/worldgen/QcWorldTypeDefinitions.h"

#include <QList>
#include <QString>

#include <optional>

/*! \brief What a world generator puts into a new world once, when the world is created (decision
    E-221): the world's rules and what its structures bring along, in world coordinates: creatures,
    contents of chests and machines, minecarts, boats, block updates to run (liquids flow, sand
    falls) and saplings that grow.

    The server applies it before the first player joins and records that in world.json. */
struct QcWorldSetup
{
    /*! A creature to place. */
    using Mob = QcStructureDefinition::Mob;
    /*! An armour stand to place. */
    using Stand = QcStructureDefinition::Stand;
    /*! The items of a container block or a container minecart. */
    using Contents = QcStructureDefinition::Contents;
    /*! A minecart to place. */
    using Cart = QcStructureDefinition::Cart;
    /*! A boat to place. */
    using Boat = QcStructureDefinition::Boat;
    /*! The text of a sign. */
    using Sign = QcStructureDefinition::Sign;
    /*! An item frame or a painting. */
    using Hanging = QcStructureDefinition::Hanging;

    /*! Seconds into the day the world starts at (QcDayCycle), else the usual morning. */
    std::optional<double> timeOfDay;
    /*! Game rule daylightCycle. */
    bool daylightCycle = true;
    /*! Game rule mobSpawning. */
    bool mobSpawning = true;
    /*! Game rule randomTicks. */
    bool randomTicks = true;
    /*! Game rule fireSpread (decision E-392). */
    bool fireSpread = true;
    /*! Game rule weatherCycle (decision E-395). */
    bool weatherCycle = true;
    /*! Game rule mobGriefing (decision E-460). */
    bool mobGriefing = true;
    /*! Game rule explosionBlockDamage (decision E-603). */
    bool explosionBlockDamage = true;
    /*! Creatures. */
    QList<Mob> mobs;
    /*! Armour stands. */
    QList<Stand> stands;
    /*! Contents of chests, machines and container minecarts. */
    QList<Contents> contents;
    /*! Blocks whose block update runs once (liquids flow, sand falls, circuits start). */
    QList<QcBlockPos> updates;
    /*! Saplings that grow. */
    QList<QcBlockPos> growing;
    /*! Minecarts on rails (decisions E-382, E-386). */
    QList<Cart> minecarts;
    /*! Boats (decision E-920). */
    QList<Boat> boats;
    /*! Text on signs (decision E-766). */
    QList<Sign> signs;
    /*! Item frames and paintings (decision E-797). */
    QList<Hanging> hangings;
    /*! The adventure allow-lists the world starts with (decision E-735; the showcase's station). */
    QcAdventureRules adventure;

    /*! Returns a setup with only the time of day, daylight cycle and creature spawning of
        \a rules, the rules of a world type (flat, void). */
    static QcWorldSetup fromRules(const QcWorldTypeRules &rules)
    {
        QcWorldSetup setup;
        setup.timeOfDay = rules.timeOfDay;
        setup.daylightCycle = rules.daylightCycle;
        setup.mobSpawning = rules.mobSpawning;
        return setup;
    }
};
