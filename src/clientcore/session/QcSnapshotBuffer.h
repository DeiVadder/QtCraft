// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <deque>

// The recent server snapshots of one entity's motion, by server time (decision E-271), and its
// pose at any moment between them: interpolated between the two around it, extrapolated along the
// last movement for at most extrapolationMs past the newest one, then held.
class QcSnapshotBuffer
{
public:
    struct Snapshot
    {
        double serverMs = 0.0;
        QcVec3 position;
        float yaw = 0.0f;
        float pitch = 0.0f;
    };

    static constexpr std::size_t capacity = 64;
    // A step this long between two snapshots is a teleport: shown at once, never in between.
    static constexpr double teleportMetres = 10.0;
    static constexpr double extrapolationMs = 100.0;

    bool isEmpty() const { return m_snapshots.empty(); }
    std::size_t size() const { return m_snapshots.size(); }
    const Snapshot &newest() const { return m_snapshots.back(); }

    // Adds the newest snapshot. `intervalMs` is the usual time between snapshots: after a longer
    // pause (an item that lay still) the motion starts one interval before the new snapshot
    // instead of creeping over the whole pause. A teleport drops the history.
    void push(const Snapshot &snapshot, double intervalMs);
    // The pose at server time `serverMs` (the newest snapshot's when the buffer is empty: none).
    Snapshot at(double serverMs) const;
    // Forgets the snapshots no longer needed for times from `serverMs` on.
    void dropBefore(double serverMs);

private:
    std::deque<Snapshot> m_snapshots;
};
