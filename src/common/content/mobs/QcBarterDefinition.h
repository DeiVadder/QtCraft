// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

/*! \brief A creature that loves gold and barters (decision E-1081; the tuskling), from its data
    (\c {barter}), with the neutral AI.

    It is hostile to every player in sight who wears none of \c {sparedBy} (armour items), as long
    as it sees him. Breaking one of the \c {guards} blocks (gold, containers) angers every adult of
    its kind within \c {alarmRange} metres at the player, opening one of them (a chest) only those
    that see him. It walks to items of \c {loves} lying within \c {seekRange} metres and picks them
    up, and it takes the \c {currency} (a gold ingot) from a player's hand. It looks at what it took
    for \c {admireSeconds}: the currency is then bartered away for one draw of the loot table
    \c {table}, thrown towards the player who gave it, other loved items it keeps (at most
    \c {hoardSize} stacks, dropped when it dies). Young ones do none of this.

    Server side only. \qcfields */
struct QcBarterDefinition
{
    QStringList sparedBy;
    QStringList loves;
    QString currency;
    QString table;
    QStringList guards;
    double admireSeconds = 6.0;
    double alarmRange = 16.0;
    double seekRange = 8.0;
    int hoardSize = 8;

    bool operator==(const QcBarterDefinition &) const = default;
};

/*! \brief Blocks a creature keeps away from (decision E-1082; the bristleboar and the warped
    fungus), from its data (\c {repelledBy}): when it looks around (every
    QcMobSteering::sightCheckTicks) and one of \c {blocks} lies within \c {range} blocks sideways
    and \c {height} blocks up or down, it flees from the nearest one instead of chasing or
    wandering. Only for the passive and hostile AI (QcPassiveBrain::think).

    Server side only. \qcfields */
struct QcRepellentDefinition
{
    QStringList blocks;
    int range = 8;
    int height = 4;

    bool operator==(const QcRepellentDefinition &) const = default;
};
