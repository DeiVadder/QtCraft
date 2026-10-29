// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/mobs/QcMobCramming.h"

#include <QHash>

#include <algorithm>
#include <functional>
#include <utility>

namespace QcMobCramming {

namespace {

// Index of a crammed creature in the creature list now; none when it is gone.
std::optional<qsizetype> indexOf(const QcMobManager &mobs, const QcMobCrowd::Crammed &one)
{
    // The index the crowd counted, unless creatures came or went in the tick since.
    const QList<QcCreature> &list = mobs.mobs();
    if (one.index < list.size() && list[one.index].id == one.id) {
        return one.index;
    }
    const auto found = std::ranges::find(list, one.id, &QcCreature::id);
    return found == list.end() ? std::nullopt : std::optional<qsizetype>(found - list.begin());
}

} // namespace

QList<Hurt> apply(QcMobManager &mobs, int limit, quint64 tick)
{
    QList<Hurt> hurt;
    QList<QcMobCrowd::Crammed> due = mobs.crammed(limit);
    due.removeIf([tick](const QcMobCrowd::Crammed &one) { return (tick + one.id) % phases != 0; });
    if (due.isEmpty()) {
        return hurt;
    }

    // The last in the list first: a death removes a creature only after those still to come.
    std::ranges::sort(due, std::greater<>(), &QcMobCrowd::Crammed::index);
    QHash<int, int> died;
    for (const QcMobCrowd::Crammed &one : std::as_const(due)) {
        const std::optional<qsizetype> index = indexOf(mobs, one);
        if (!index || one.count - died.value(one.cell) <= limit || mobs.mobs()[*index].hurt.wouldTake(damage) == QcHurtWindow::Take::Ignored) {
            continue;
        }

        const QcCreature &mob = mobs.mobs()[*index];
        Hurt entry{mob.id, mob.type, mob.motion.position, std::nullopt};
        entry.drops = mobs.woundAt(*index, damage);
        if (entry.drops) {
            died[one.cell] += 1;
        }
        hurt.append(std::move(entry));
    }
    return hurt;
}

} // namespace QcMobCramming
