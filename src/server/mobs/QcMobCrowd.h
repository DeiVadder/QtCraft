// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcMobDefinition.h"
#include "common/core/QcBlockPos.h"
#include "server/core/QcServerLimits.h"
#include "server/entities/QcCreature.h"

#include <QHash>
#include <QList>

#include <span>
#include <vector>

// Crowds of creatures (decision E-401). At the start of each tick the awake walkers (flyers, water
// creatures of decision E-502 and dormant ones take no part) are counted into cells of one block,
// by the block their feet are in, with the sum of their positions and half widths. A creature is
// pushed away from the centre of the others in its cell and from the centre of each of the eight
// cells around it on the same level, as far as it overlaps them: one push per cell instead of one
// per pair, so the cost grows with the number of creatures (nine cell lookups each), however
// densely they stand. Creatures have no collision with each other; this push is all that keeps
// them apart.
//
// Hoverers that chase someone (decision E-839) form a crowd of their own (Members::Hoverers): only
// the push keeps them apart (the same cells, by the block their feet are in), the tiers and cramming
// are the walkers'. Children orbiting their parent chase nobody and take no part, so the parent in
// the middle of their circle is not pushed.
//
// The number in a creature's cell also decides its AI tier (Tier, thresholds QcServerLimits
// crowdThrottled and crowdIdle) and whether the cell is crammed (game rule maxEntityCramming).
class QcMobCrowd
{
public:
    // Full: everything. Throttled: thinks only every thinkInterval() ticks, without path searches,
    // and keeps its last steering in between. Idle: no AI (gravity, currents, damage and the push only).
    // A blow at a directly adjacent player stays in every tier.
    enum class Tier : quint8 { Full, Throttled, Idle };

    // How strongly an overlap turns the steering (1: a full overlap outweighs the wish to walk).
    static constexpr double strength = 1.5;
    // A throttled creature thinks every this many ticks at the throttled threshold, up to the most
    // at the idle threshold.
    static constexpr int fewestThinkTicks = 4;
    static constexpr int mostThinkTicks = 10;

    // Who takes part: the awake walkers, or the awake hoverers (QcMobDefinition::hover) that chase
    // someone.
    enum class Members : quint8 { Walkers, Hoverers };

    explicit QcMobCrowd(Members members = Members::Walkers)
        : m_kind(members)
    {
    }

    void setThresholds(qint64 throttled, qint64 idle);

    // Counts the awake members of `mobs` where they stand at the start of the tick; the other calls
    // take the index of a creature in this list.
    void build(std::span<const QcCreature> mobs, const QList<QcMobDefinition> &definitions);

    // Creatures in the cell of the creature (itself included); 0 for one that takes no part.
    int countAt(qsizetype index) const;
    Tier tierAt(qsizetype index) const;
    // Ticks between two thoughts of a throttled creature, more the denser its cell.
    int thinkInterval(qsizetype index) const;
    // Horizontal push of `self` (where it stands now, half width `halfWidth`) away from the others
    // in its cell and the cells around it, as they stood when build() ran; zero when none overlaps.
    QcVec3 push(qsizetype index, const QcCreature &self, double halfWidth) const;
    // A creature in a cell with more than the limit of them: its id, its index in the list build()
    // counted, its cell (an index) and the number in the cell.
    struct Crammed
    {
        quint32 id = 0;
        qsizetype index = 0;
        int cell = 0;
        int count = 0;
    };
    // The creatures in cells with more than `limit` of them (game rule maxEntityCramming; 0 or
    // less: none), in the order of the list.
    QList<Crammed> crammed(int limit) const;
    // The most creatures in one cell (tests, debug).
    int largestCell() const;

private:
    struct Cell
    {
        QcBlockPos pos;
        int count = 0;
        double sumX = 0.0;
        double sumZ = 0.0;
        double sumHalfWidth = 0.0;
    };

    // A creature of the list as build() saw it; `cell` is -1 for one that takes no part.
    struct Member
    {
        int cell = -1;
        quint32 id = 0;
        double x = 0.0;
        double z = 0.0;
        double halfWidth = 0.0;
    };

    // Whether the awake creature `mob` of `definition` belongs to this crowd.
    bool takesPart(const QcCreature &mob, const QcMobDefinition &definition) const;
    // The push from the creatures of one cell, `others` of them with these sums.
    static QcVec3 pushFrom(const QcCreature &self, double halfWidth, int others, const Cell &sums, const Member *exclude);

    std::vector<Cell> m_cells;
    QHash<QcBlockPos, int> m_cellIndex;
    std::vector<Member> m_members;
    Members m_kind = Members::Walkers;
    int m_throttled = static_cast<int>(QcServerLimits{}.crowdThrottled);
    int m_idle = static_cast<int>(QcServerLimits{}.crowdIdle);
};
