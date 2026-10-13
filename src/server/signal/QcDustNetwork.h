#pragma once

#include "QcSignalPower.h"

#include <QHash>
#include <QList>

#include <vector>

// One-pass recalculation of Signal Dust (decision E-335). Dust is joined by links: flat to the dust
// next to it, up the side of a block when nothing opaque lies on the lower dust, down the side of a
// block that is not opaque. Strength falls by one per link, so a change can only reach dust within
// 15 links of where it happened: that part of the network is the region recomputed, the dust just
// outside keeps its strength and feeds the region like a source. The region's strengths come from
// one breadth-first pass from the strongest inputs down (each dust and link looked at once), its
// connections (dot, line, cross, slopes) in the same pass, and only dust whose state really changes
// is reported. A long wire therefore costs the same as a short one, and the result does not depend
// on where the wire lies, how it is turned or in which order its dust is visited.
class QcDustNetwork
{
public:
    // Dust per region at most; beyond, the rest of a pathological region keeps its old state.
    static constexpr int maxNodes = 65536;

    struct Change
    {
        QcBlockPos pos;
        QcBlockId state = qcAirBlockId;
    };

    struct Result
    {
        QList<Change> changes;
        // Dust looked at (the work done).
        qsizetype visited = 0;
        // Parts of the region (dust linked to each other) that reach a chunk that is not loaded: they
        // were not recomputed and wait for that chunk, with the seeds among them. Parts not linked to
        // them are recomputed as usual (they could not change each other).
        struct Blocked
        {
            QcChunkPos chunk;
            QList<QcBlockPos> seeds;
        };
        QList<Blocked> blocked;
    };

    explicit QcDustNetwork(const QcSignalPower &power);

    // The sides the dust at `pos` shows: flat to dust and to sources and repeaters lined up with it,
    // up to dust on the neighbouring block, down to dust beside a block that is not opaque; a single
    // connection becomes a line through the dust.
    std::array<QcDustSide, 4> sidesOf(QcBlockPos pos) const;
    // The dust the dust at `pos` is linked to.
    QList<QcBlockPos> links(QcBlockPos pos) const;

    // Recomputes the dust within 15 links of the dust among `seeds` (other positions are ignored);
    // changed dust goes into `result`.
    void recompute(const std::vector<QcBlockPos> &seeds, Result &result) const;

private:
    struct Region;

    bool isDust(QcBlockPos pos) const { return m_power.at(pos).kind == QcSignalKind::Dust; }
    // The dust within reach of the seeds, with links inside the region as indices and the strength
    // that dust just outside the region gives each node.
    Region collect(const std::vector<QcBlockPos> &seeds) const;
    // The linked parts of the region: a part number per node.
    static QList<int> parts(const Region &region, int &count);
    static void markPart(const Region &region, qsizetype start, int number, QList<int> &part);
    // Per part the unloaded chunk it reads, if any; adds the waiting parts with their seeds to `result`.
    static QList<std::optional<QcChunkPos>> blockedParts(const Region &region, const QList<int> &part, int count, Result &result);
    // Strength of every dust of the region, strongest first.
    static QList<int> strengths(const Region &region, QList<int> inputs);
    // Gives `level` to the linked dust that has less and puts it into that level's bucket.
    static void spread(const QList<int> &linked, int level, QList<int> &inputs, QList<int> &bucket);

    const QcSignalPower &m_power;
};
