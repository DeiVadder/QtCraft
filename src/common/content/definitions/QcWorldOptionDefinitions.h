// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QMap>
#include <QString>

#include <optional>

// The options of the world generation a player sets when creating a world (decision E-917,
// worldgen/options.json of each pack): each option sets one target of the generator's content
// values (QcWorldOptionTargets: the village spacing, the ore amount, caves on or off …) for that
// world, in place of the value the content gives. Server side only.

// How a number option's value reads in the menu.
enum class QcWorldOptionUnit {
    // The number as it is.
    Plain,
    // A distance in blocks ("384 blocks").
    Blocks,
    // A share in 0..1, shown in percent ("65 %").
    Percent,
    // A factor on the content's amount ("1.5×").
    Factor,
};

// A heading the options of the menu are grouped under.
struct QcWorldOptionGroup
{
    QString name;
    QString translationKey;
};

struct QcWorldOptionDefinition
{
    QString name;
    // The label; "<translationKey>.help" is the help line below it.
    QString translationKey;
    // The qualified name of its group (QcWorldOptionGroup).
    QString group;
    // The generator value it sets (QcWorldOptionTargets) and, for targets about one definition, the
    // qualified name of that definition (an ore).
    QString target;
    QString subject;
    QcWorldOptionUnit unit = QcWorldOptionUnit::Plain;
    // The range of a number option and the step of its slider (0: any value); switches ignore them.
    double min = 0.0;
    double max = 1.0;
    double step = 0.0;
    // The value a new world starts with; missing: the target's value in the content (1 for factors),
    // so that a world with every option at its default generates as the content alone would.
    std::optional<double> defaultValue;
};

// A named set of option values (more villages, large biomes): the other options keep their defaults.
struct QcWorldPreset
{
    QString name;
    QString translationKey;
    // Qualified option name -> value (switches: 0 or 1).
    QMap<QString, double> values;
};

// What one pack's worldgen/options.json adds; a later pack replaces groups, options and presets of
// the same name.
struct QcWorldOptionSet
{
    QList<QcWorldOptionGroup> groups;
    QList<QcWorldOptionDefinition> options;
    QList<QcWorldPreset> presets;
};
