// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

// One step a player can take (decision E-1096): with all of `needs` reached he gets `gives`. Several
// ways to the same node are several rules. Nodes are readable names ("item qtcraft:stick", "block
// qtcraft:stone", "pickaxe of level 2"), so what a report names is what is missing.
struct QcProgressionRule
{
    // What the step is, for the path of a report ("recipe qtcraft:stick").
    QString step;
    QStringList needs;
    QStringList gives;
    // How much it gives: `quantity` when set, else `perUnit` for each `scaledBy` there is, else as
    // much as anyone wants.
    qint64 quantity = 0;
    QString scaledBy = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisations warning-free
    int perUnit = 1;
    // When set, the rule opens only with at least this much of the first of `needs`.
    qint64 atLeast = 0;
};

// What is missing on the way to a node that was not reached.
struct QcProgressionGap
{
    // What nothing gives, nearest first: a node, or nodes that only come from each other ("block a
    // / item a": a block that drops its own item, which places it). Each of them is one way to open
    // the path.
    QStringList missing;
    // From the node to the first of `missing`: nodes with the step between them.
    QStringList path;
};

// Everything the rules reach from the start nodes, and why the rest is out of reach.
class QcProgressionSolver
{
public:
    // A supply nobody can use up.
    static constexpr qint64 unlimited = qint64{1} << 40;

    QcProgressionSolver(QList<QcProgressionRule> rules, const QSet<QString> &start);

    [[nodiscard]] bool reached(const QString &node) const { return m_reached.contains(node); }
    // How much of `node` a player can have: the sum of the ways open to him.
    [[nodiscard]] qint64 supply(const QString &node) const;
    // One way to `node`, which was reached: the steps in the order a player takes them.
    [[nodiscard]] QStringList wayTo(const QString &node) const;
    // What stands between the player and `node`, which was not reached.
    [[nodiscard]] QcProgressionGap gapTo(const QString &node) const;

private:
    [[nodiscard]] bool canOpen(const QcProgressionRule &rule) const;
    // What `rule`, which is open, gives; `seen` keeps a way from going round in a circle.
    [[nodiscard]] qint64 yieldOf(const QcProgressionRule &rule, const QSet<QString> &seen) const;
    [[nodiscard]] qint64 supplyAlong(const QString &node, QSet<QString> seen) const;
    // The nodes that stop the rule at `index`: a node there is too little of counts too.
    [[nodiscard]] QStringList unmetNeeds(qsizetype index) const;
    void solve();

    QList<QcProgressionRule> m_rules;
    QSet<QString> m_reached;
    QSet<QString> m_start;
    // Node -> the positions in m_rules of the rules that give it.
    QHash<QString, QList<qsizetype>> m_givers;
    // The positions in m_rules of the rules that are open.
    QSet<qsizetype> m_open;
    // Node -> the position in m_rules of the rule that reached it.
    QHash<QString, qsizetype> m_firstGiver;
};
