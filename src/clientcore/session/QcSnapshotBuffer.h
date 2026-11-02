// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <deque>

/*! \brief The recent server snapshots of one entity's motion, by server time (decision E-271),
    and its pose at any moment between them: interpolated between the two around it, extrapolated
    along the last movement for at most extrapolationMs past the newest one, then held. */
class QcSnapshotBuffer
{
public:
    /*! \brief The motion of the entity at one moment of server time. */
    struct Snapshot
    {
        /*! The server time of the snapshot, in milliseconds. */
        double serverMs = 0.0;
        /*! Where the entity was. */
        QcVec3 position;
        /*! Where it faced, in degrees. */
        float yaw = 0.0f;
        /*! Its head pitch, in degrees. */
        float pitch = 0.0f;
    };

    /*! Snapshots kept at most; the oldest go first. */
    static constexpr std::size_t capacity = 64;
    /*! A step this long between two snapshots is a teleport: shown at once, never in between. */
    static constexpr double teleportMetres = 10.0;
    /*! How far past the newest snapshot the motion is extrapolated before it is held. */
    static constexpr double extrapolationMs = 100.0;

    /*! Returns whether the buffer holds no snapshot. */
    bool isEmpty() const { return m_snapshots.empty(); }
    /*! Returns the number of snapshots held. */
    std::size_t size() const { return m_snapshots.size(); }
    /*! Returns the newest snapshot; the buffer must not be empty. */
    const Snapshot &newest() const { return m_snapshots.back(); }

    /*! Adds the newest \a snapshot. \a intervalMs is the usual time between snapshots: after a
        longer pause (an item that lay still) the motion starts one interval before the new
        snapshot instead of creeping over the whole pause. A teleport or a snapshot older than
        the last drops the history. */
    void push(const Snapshot &snapshot, double intervalMs);
    /*! Returns the pose at server time \a serverMs: interpolated between the snapshots around
        it, the oldest before the oldest, and past the newest the last movement continued for
        at most extrapolationMs (yaw and pitch held), then held. An empty buffer gives a zero
        snapshot at \a serverMs. */
    Snapshot at(double serverMs) const;
    /*! Forgets the snapshots no longer needed for times from \a serverMs on. */
    void dropBefore(double serverMs);

private:
    std::deque<Snapshot> m_snapshots;
};
