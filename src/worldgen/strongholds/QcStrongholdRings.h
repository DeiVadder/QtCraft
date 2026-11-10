// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcXzRect.h"

#include <QList>

#include <array>
#include <functional>
#include <mutex>
#include <optional>

/*! \brief Where the 128 strongholds of a world stand (decision E-1170): on eight rings around the
    origin as in the original game, each ring with a start angle from the seed, its strongholds at
    even angles and each at a distance from the seed within the ring's band, then moved to the
    nearest land within nudgeReach blocks (no sea, river or delta).

    A pure function of the seed and the terrain noise, never of generated chunks: the eye and
    /locate find a stronghold before its chunks exist. A ring is worked out once, when first
    asked. Thread-safe. */
class QcStrongholdRings
{
public:
    /*! Returns whether the column \a x, \a z is land a stronghold may stand on. */
    using LandTest = std::function<bool(int x, int z)>;

    /*! \brief A ring: \c {count} strongholds between \c {minDistance} and \c {maxDistance}
        blocks from the origin. \qcfields */
    struct Ring
    {
        int count = 0;
        int minDistance = 0;
        int maxDistance = 0;
    };

    /*! The rings of the original game. */
    static constexpr std::array<Ring, 8> rings{{{3, 1280, 2816},
                                                {6, 4352, 5888},
                                                {10, 7424, 8960},
                                                {15, 10496, 12032},
                                                {21, 13568, 15104},
                                                {28, 16640, 18176},
                                                {36, 19712, 21248},
                                                {9, 22784, 24320}}};
    /*! The strongholds of a world. */
    static constexpr int count = 128;
    /*! How far a stronghold moves at most to stand on land. */
    static constexpr int nudgeReach = 112;

    /*! Places the strongholds of \a seed on the land \a land finds, which outlives it. */
    QcStrongholdRings(quint64 seed, LandTest land);

    /*! Returns the ring of the stronghold \a index. */
    [[nodiscard]] static int ringOf(int index);
    /*! Returns the index of the first stronghold of \a ring. */
    [[nodiscard]] static int firstOf(int ring);
    /*! Returns where the stronghold \a i of \a ring stands before it moves to land: the middle of
        a chunk, from \a seed alone. */
    [[nodiscard]] static QcBlockPos rawPosition(quint64 seed, int ring, int i);

    /*! Returns the column of the stronghold \a index (y 0), working out its ring once. */
    [[nodiscard]] QcBlockPos positionOf(int index) const;
    /*! Returns the strongholds whose column lies within \a margin blocks of \a area, working out
        the rings they may stand on. */
    [[nodiscard]] QList<int> near(QcXzRect area, int margin) const;
    /*! Returns near() of \a area and \a margin from the rings worked out so far, never working
        one out; none when a ring it needs is not worked out yet. */
    [[nodiscard]] std::optional<QList<int>> knownNear(QcXzRect area, int margin) const;
    /*! Returns the stronghold nearest to \a from, working out every ring it needs. */
    [[nodiscard]] int nearest(QcBlockPos from) const;
    /*! Returns the stronghold nearest to \a from that \a accept takes (called with its index,
        the nearest first), working out every ring it needs; none when \a accept takes none. */
    [[nodiscard]] std::optional<int> nearest(QcBlockPos from, const std::function<bool(int index)> &accept) const;

private:
    // The rings `area` within `margin` may hold a stronghold of.
    [[nodiscard]] static QList<int> ringsNear(QcXzRect area, int margin);
    // The positions of `ring`, worked out now.
    [[nodiscard]] QList<QcBlockPos> place(int ring) const;
    // The positions of `ring`, worked out once.
    [[nodiscard]] QList<QcBlockPos> positions(int ring) const;
    // The positions of `ring` if worked out.
    [[nodiscard]] std::optional<QList<QcBlockPos>> known(int ring) const;
    // The indices of `positions` of `ring` within `margin` of `area`.
    [[nodiscard]] static QList<int> within(const QList<QcBlockPos> &positions, int ring, QcXzRect area, int margin);
    [[nodiscard]] quint64 seed() const { return m_seed; }

    quint64 m_seed = 0;
    LandTest m_land;
    // Every thread hands the rings it works out to the others under this mutex.
    mutable std::mutex m_mutex;
    mutable std::array<std::optional<QList<QcBlockPos>>, rings.size()> m_rings; // NOLINT(qc-member-access): read and written only in known() and positions()
};
