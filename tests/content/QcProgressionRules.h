// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "content/QcProgressionSolver.h"
#include "content/QcWorldCensus.h"

#include <QList>
#include <QString>
#include <QStringList>

class QcContentRegistry;
struct QcProgressionGoal;

// The names of the nodes of the progression graph.
namespace QcProgressionNode {

[[nodiscard]] inline QString item(const QString &name)
{
    return QStringLiteral("item ") + name;
}
[[nodiscard]] inline QString block(const QString &name)
{
    return QStringLiteral("block ") + name;
}
[[nodiscard]] inline QString creature(const QString &name)
{
    return QStringLiteral("creature ") + name;
}
// Chests filled from the loot table `name`.
[[nodiscard]] inline QString chest(const QString &name)
{
    return QStringLiteral("chest ") + name;
}
[[nodiscard]] inline QString dimension(const QString &name)
{
    return QStringLiteral("dimension ") + name;
}
[[nodiscard]] inline QString portal(const QString &name)
{
    return QStringLiteral("portal ") + name;
}
// A villager to trade with: a village.
[[nodiscard]] inline QString villagers()
{
    return QStringLiteral("villagers");
}
// Something that can be eaten.
[[nodiscard]] inline QString food()
{
    return QStringLiteral("food");
}
// Something that lights fire in a cell of the player's choice, as QcServerFire::ignite does: fire
// that spreads does not ask the portals (QcServerFire::light), so it lights neither a frame nor
// counts here.
[[nodiscard]] inline QString lighter()
{
    return QStringLiteral("something that lights");
}

} // namespace QcProgressionNode

// How a goal came out for one start dimension.
struct QcProgressionResult
{
    QString goal;
    bool met = false;
    // What is missing (see QcProgressionGap), the shortest way there first.
    QStringList missing;
    QStringList path;
    // When it was met: one way there, in the order a player takes the steps.
    QStringList way;

    // One line for a test's log.
    [[nodiscard]] QString describe() const;
};

// The progression graph of a content and the worlds it generates (decision E-1096).
namespace QcProgressionRules {

// The rules of `content`: recipes, smelting, the stonecutter, what blocks drop and with which tool,
// what creatures drop and barter, what chests hold, villagers' trades, liquids and portals.
[[nodiscard]] QList<QcProgressionRule> ofContent(const QcContentRegistry &content);
// The node a goal asks for.
[[nodiscard]] QString nodeOf(const QcProgressionGoal &goal);
// How the goals of `content` come out for a player who starts in `startDimension` with the
// `censuses` of all dimensions of a world, from `rules` (ofContent, once per content).
[[nodiscard]] QList<QcProgressionResult> check(const QcContentRegistry &content, const QList<QcProgressionRule> &rules,
                                               const QList<QcWorldCensus> &censuses, const QString &startDimension);

} // namespace QcProgressionRules
