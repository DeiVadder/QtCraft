#include "QcSnapshotBuffer.h"

#include <algorithm>
#include <cmath>

namespace {

// Turns the short way round, in degrees.
float lerpAngle(float from, float to, double t)
{
    const float difference = std::remainder(to - from, 360.0f);
    return from + difference * float(t);
}

QcSnapshotBuffer::Snapshot lerp(const QcSnapshotBuffer::Snapshot &from, const QcSnapshotBuffer::Snapshot &to, double serverMs)
{
    const double span = to.serverMs - from.serverMs;
    const double t = span > 0.0 ? (serverMs - from.serverMs) / span : 1.0;
    return {serverMs, from.position + (to.position - from.position) * t, lerpAngle(from.yaw, to.yaw, t), from.pitch + (to.pitch - from.pitch) * float(t)};
}

} // namespace

void QcSnapshotBuffer::push(const Snapshot &snapshot, double intervalMs)
{
    if (!m_snapshots.empty()) {
        const Snapshot &last = m_snapshots.back();
        if ((snapshot.position - last.position).length() > teleportMetres || snapshot.serverMs < last.serverMs) {
            m_snapshots.clear();
        } else if (snapshot.serverMs == last.serverMs) {
            // Two snapshots of one tick: the later one counts.
            m_snapshots.pop_back();
        } else if (snapshot.serverMs - last.serverMs > 2.5 * intervalMs) {
            // A pause: stand still until one interval before the new snapshot.
            Snapshot standing = last;
            standing.serverMs = snapshot.serverMs - intervalMs;
            m_snapshots.push_back(standing);
        }
    }

    m_snapshots.push_back(snapshot);
    while (m_snapshots.size() > capacity) {
        m_snapshots.pop_front();
    }
}

QcSnapshotBuffer::Snapshot QcSnapshotBuffer::at(double serverMs) const
{
    if (m_snapshots.empty()) {
        return {serverMs, {}, 0.0f, 0.0f};
    }

    if (serverMs <= m_snapshots.front().serverMs) {
        return m_snapshots.front();
    }

    const auto after = std::ranges::find_if(m_snapshots, [serverMs](const Snapshot &snapshot) { return snapshot.serverMs >= serverMs; });
    if (after != m_snapshots.end()) {
        return lerp(*std::prev(after), *after, serverMs);
    }

    // Past the newest: continue its last movement for a moment (a late snapshot), then hold.
    const Snapshot &newest = m_snapshots.back();
    if (m_snapshots.size() < 2) {
        return newest;
    }
    const Snapshot &before = m_snapshots[m_snapshots.size() - 2];
    const double ahead = std::min(serverMs, newest.serverMs + extrapolationMs);
    Snapshot extrapolated = lerp(before, newest, ahead);
    // The view turns only between real snapshots.
    extrapolated.yaw = newest.yaw;
    extrapolated.pitch = newest.pitch;
    return extrapolated;
}

void QcSnapshotBuffer::dropBefore(double serverMs)
{
    // Keep the last snapshot at or before the time (it starts the interpolation) and always the
    // newest two (their movement continues past the newest).
    while (m_snapshots.size() > 2 && m_snapshots[1].serverMs <= serverMs) {
        m_snapshots.pop_front();
    }
}
