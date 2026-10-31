// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/signal/QcSignalPower.h"

#include <QHash>
#include <QList>

#include <vector>

/*! \brief One-pass recalculation of Signal Dust (decision E-335).

    Dust is joined by links: flat to the dust next to it, up the side of a block when nothing
    opaque lies on the lower dust, down the side of a block that is not opaque; the signal only
    goes down such a link from dust on a conductor. Strength falls by one per link, so a change
    can only reach dust within 15 links of where it happened: that part of the network is the
    region recomputed, the dust just outside keeps its strength and feeds the region like a
    source. The region's strengths come from one breadth-first pass from the strongest inputs
    down (each dust and link looked at once), its connections (dot, line, cross, slopes) in the
    same pass, and only dust whose state really changes is reported. A long wire therefore costs
    the same as a short one, and the result does not depend on where the wire lies, how it is
    turned or in which order its dust is visited. */
class QcDustNetwork
{
public:
    /*! Dust per region at most; beyond, the rest of a pathological region keeps its old state. */
    static constexpr int maxNodes = 65536;

    /*! \brief The new \c {state} of the dust at \c {pos}. \qcfields */
    struct Change
    {
        QcBlockPos pos;
        QcBlockId state = qcAirBlockId;
    };

    /*! \brief What a recomputation found. */
    struct Result
    {
        /*! The dust whose state changes. */
        QList<Change> changes;
        /*! Dust looked at (the work done). */
        qsizetype visited = 0;
        /*! \brief Parts of the region (dust linked to each other) that reach a chunk that is not
            loaded: they were not recomputed and wait for that chunk, with the seeds among them.

            Parts not linked to them are recomputed as usual (they could not change each other).
            \c {chunk} is the chunk they wait for. \qcfields */
        struct Blocked
        {
            QcChunkPos chunk;
            QList<QcBlockPos> seeds;
        };
        /*! The parts that wait for a chunk. */
        QList<Blocked> blocked;
    };

    explicit QcDustNetwork(const QcSignalPower &power);

    /*! Returns the sides the dust at \a pos shows: flat to dust and to sources and repeaters
        lined up with it, up to dust on the neighbouring block, down to dust beside a block that
        is not opaque; a single connection becomes a line through the dust. */
    std::array<QcDustSide, 4> sidesOf(QcBlockPos pos) const;
    /*! Returns the dust the dust at \a pos is linked to. */
    QList<QcBlockPos> links(QcBlockPos pos) const;
    /*! Returns whether the dust at \a from gives its strength to the linked dust at \a to. A
        link down the side of a block only carries the signal down when the upper dust lies on a
        conductor; dust on glass or an upper slab passes it up a stair but not down (decision
        E-887, like the role model). */
    bool feeds(QcBlockPos from, QcBlockPos to) const;

    /*! Recomputes the dust within 15 links of the dust among \a seeds (other positions are
        ignored). Returns the dust that changes and the parts that wait for a chunk. */
    [[nodiscard]] Result recompute(const std::vector<QcBlockPos> &seeds) const;

private:
    struct Region;

    bool isDust(QcBlockPos pos) const { return m_power.at(pos).kind == QcSignalKind::Dust; }
    // The dust within reach of the seeds, with links inside the region as indices and the strength
    // that dust just outside the region gives each node.
    Region collect(const std::vector<QcBlockPos> &seeds) const;
    // The linked parts of a region: a part number per node (-1: not yet in a part) and the number
    // of parts.
    struct Parts
    {
        QList<int> of;
        int count = 0;

        // Gives the next part number to the node `start` and all dust linked to it.
        void add(const Region &region, qsizetype start);
    };

    // Per part the unloaded chunk it reads, if any, and the waiting parts with their seeds.
    struct Blocking
    {
        QList<std::optional<QcChunkPos>> ofPart;
        QList<Result::Blocked> waiting;
    };

    // The linked parts of the region.
    [[nodiscard]] static Parts parts(const Region &region);
    [[nodiscard]] static Blocking blockedParts(const Region &region, const Parts &parts);
    // Strength of every dust of the region, strongest first.
    [[nodiscard]] static QList<int> strengths(const Region &region, QList<int> inputs);

    const QcSignalPower &m_power;
};
