// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "content/QcProgressionSolver.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcProgressionSolver::QcProgressionSolver(QList<QcProgressionRule> rules, const QSet<QString> &start)
    : m_rules(std::move(rules))
    , m_reached(start)
    , m_start(start)
{
    for (qsizetype index = 0; index < m_rules.size(); ++index) {
        for (const QString &node : m_rules.at(index).gives) {
            m_givers[node].append(index);
        }
    }
    solve();
}

bool QcProgressionSolver::canOpen(const QcProgressionRule &rule) const
{
    const bool needsMet = std::ranges::all_of(rule.needs, [this](const QString &node) { return reached(node); });
    return needsMet && (rule.atLeast == 0 || supply(rule.needs.first()) >= rule.atLeast);
}

void QcProgressionSolver::solve()
{
    // Rules wait until their needs are met; every pass opens at least one rule or ends. A rule that
    // opens can only add to a supply, so one that waits for an amount gets its chance in the next pass.
    QList<qsizetype> waiting;
    for (qsizetype index = 0; index < m_rules.size(); ++index) {
        waiting.append(index);
    }

    bool opened = true;
    while (opened) {
        opened = false;
        QList<qsizetype> still;
        for (const qsizetype index : std::as_const(waiting)) {
            const QcProgressionRule &rule = m_rules.at(index);
            if (canOpen(rule)) {
                m_open.insert(index);
                for (const QString &node : rule.gives) {
                    if (!m_reached.contains(node)) {
                        m_reached.insert(node);
                        m_firstGiver.insert(node, index);
                    }
                }
                opened = true;
            } else {
                still.append(index);
            }
        }
        waiting = std::move(still);
    }
}

qint64 QcProgressionSolver::yieldOf(const QcProgressionRule &rule, const QSet<QString> &seen) const
{
    if (rule.quantity > 0) {
        return rule.quantity;
    }
    if (rule.scaledBy.isEmpty()) {
        return unlimited;
    }
    return std::min(unlimited, supplyAlong(rule.scaledBy, seen) * rule.perUnit);
}

qint64 QcProgressionSolver::supplyAlong(const QString &node, QSet<QString> seen) const
{
    if (m_start.contains(node)) {
        return unlimited;
    }
    if (seen.contains(node)) {
        return 0;
    }

    seen.insert(node);
    qint64 total = 0;
    const QList<qsizetype> givers = m_givers.value(node);
    for (const qsizetype index : givers) {
        if (m_open.contains(index)) {
            total = std::min(unlimited, total + yieldOf(m_rules.at(index), seen));
        }
    }
    return total;
}

qint64 QcProgressionSolver::supply(const QString &node) const
{
    return supplyAlong(node, {});
}

QStringList QcProgressionSolver::unmetNeeds(qsizetype index) const
{
    const QcProgressionRule &rule = m_rules.at(index);
    QStringList unmet;
    std::ranges::copy_if(rule.needs, std::back_inserter(unmet), [this](const QString &node) { return !reached(node); });
    return unmet.isEmpty() && !m_open.contains(index) ? rule.needs : unmet;
}

QStringList QcProgressionSolver::wayTo(const QString &node) const
{
    // Depth first over the rules that first gave each node: needs before what they open.
    QStringList way;
    QSet<QString> seen;
    const auto visit = [this, &way, &seen](const auto &self, const QString &current) -> void {
        if (seen.contains(current) || !m_firstGiver.contains(current)) {
            return;
        }
        seen.insert(current);
        const QcProgressionRule &rule = m_rules.at(m_firstGiver.value(current));
        for (const QString &need : rule.needs) {
            self(self, need);
        }
        if (!way.contains(rule.step)) {
            way.append(rule.step);
        }
    };
    visit(visit, node);
    return way;
}

namespace {

// What stops the ways to a node: every unreached node behind it, nearest first, with what stops it.
struct Blockers
{
    struct Link
    {
        QString from;
        QString step;
    };

    QStringList nodes;
    // Node -> the unreached nodes one of its ways needs.
    QHash<QString, QStringList> needs;
    // Node -> how the search came to it.
    QHash<QString, Link> links;

    // The nodes `start` leads to, itself included.
    [[nodiscard]] QSet<QString> behind(const QString &start) const
    {
        QSet<QString> seen{start};
        QStringList queue{start};
        while (!queue.isEmpty()) {
            const QStringList next = needs.value(queue.takeLast());
            for (const QString &need : next) {
                if (!seen.contains(need)) {
                    seen.insert(need);
                    queue.append(need);
                }
            }
        }
        return seen;
    }

    // The ends of the search, nearest first: nodes, and circles of nodes (an item and the block it
    // places), that lead to nothing else.
    [[nodiscard]] QList<QStringList> ends() const
    {
        QHash<QString, QSet<QString>> reach;
        for (const QString &node : nodes) {
            reach.insert(node, behind(node));
        }

        QList<QStringList> found;
        QSet<QString> done;
        for (const QString &node : nodes) {
            const QSet<QString> &own = reach.value(node);
            const bool closed = std::ranges::all_of(own, [&reach, &node](const QString &other) { return reach.value(other).contains(node); });
            if (closed && !done.contains(node)) {
                QStringList circle = own.values();
                circle.sort();
                circle.removeOne(node);
                circle.prepend(node);
                found.append(circle);
                done.unite(own);
            }
        }
        return found;
    }

    // From the start of the search to `end`: nodes with the steps between them.
    [[nodiscard]] QStringList pathTo(const QString &end) const
    {
        QStringList path{end};
        QString at = end;
        while (links.contains(at)) {
            const Link link = links.value(at);
            path.prepend(link.step);
            path.prepend(link.from);
            at = link.from;
        }
        return path;
    }
};

} // namespace

QcProgressionGap QcProgressionSolver::gapTo(const QString &node) const
{
    // Breadth first, so the nearest end comes first.
    Blockers blockers;
    blockers.nodes.append(node);
    for (qsizetype next = 0; next < blockers.nodes.size(); ++next) {
        const QString current = blockers.nodes.at(next);
        const QList<qsizetype> givers = m_givers.value(current);
        for (const qsizetype index : givers) {
            const QStringList unmet = unmetNeeds(index);
            for (const QString &need : unmet) {
                blockers.needs[current].append(need);
                if (need != node && !blockers.links.contains(need)) {
                    blockers.links.insert(need, {current, m_rules.at(index).step});
                    blockers.nodes.append(need);
                }
            }
        }
    }

    QcProgressionGap gap;
    const QList<QStringList> ends = blockers.ends();
    for (const QStringList &end : ends) {
        gap.missing.append(end.join(u" / "_s));
    }
    gap.path = blockers.pathTo(ends.first().first());
    return gap;
}
