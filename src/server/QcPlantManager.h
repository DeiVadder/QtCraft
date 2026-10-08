#pragma once

#include "QcBlockAccess.h"
#include "QcContentRegistry.h"
#include "QcTreeShape.h"

#include <QHash>
#include <QList>
#include <QRandomGenerator>
#include <QSet>

#include <functional>
#include <memory>

// Living plants on the server: leaves of trees decay when no trunk is near any more, and planted
// saplings grow into the tree feature they name. Trunks and leaves are the blocks that the tree
// features of the content use.
//
// Leaves are only checked after a trunk block nearby was removed, a few at a time and with a
// short random delay, so felling a tree makes its crown fall apart gradually. Saplings grow only
// while their chunk is loaded.
class QcPlantManager
{
public:
    // Leaves stay as long as a trunk block lies within this many blocks along every axis.
    static constexpr int leafReach = 4;
    static constexpr double minDecayDelay = 0.5;
    static constexpr double maxDecayDelay = 4.0;
    static constexpr int maxDecaysPerTick = 8;
    static constexpr int maxChecksPerTick = 64;
    // Leaves next to unloaded chunks are checked again after this many seconds.
    static constexpr double recheckSeconds = 5.0;
    static constexpr int maxPendingChecks = 16384;
    static constexpr int maxSaplings = 4096;
    // A sapling without room to grow tries again after this many seconds.
    static constexpr double retrySeconds = 30.0;

    struct Sapling
    {
        QcBlockPos pos;
        // Seconds of loaded time until it tries to grow.
        double remaining = 0.0;
    };

    // Sets a block and tells the clients; returns false when it could not be set.
    using SetBlock = std::function<bool(QcBlockPos pos, QcBlockId block)>;
    // Removes a block as if it broke by itself, dropping its items.
    using BreakBlock = std::function<void(QcBlockPos pos)>;
    using LoadedFunction = std::function<bool(QcBlockPos pos)>;

    QcPlantManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed);

    bool isLeaves(QcBlockId block) const { return m_leaves.contains(block); }
    bool isTrunk(QcBlockId block) const { return m_trunks.contains(block); }
    // Whether `sapling` may stand on `ground`; false for blocks that are not saplings.
    bool canPlant(QcBlockId sapling, std::optional<QcBlockId> ground) const;

    // Tell the manager that the block `previous` at `pos` was removed.
    void blockRemoved(QcBlockPos pos, QcBlockId previous, const QcBlockAccess &world);
    // Starts the growth timer of a sapling block just placed at `pos`.
    void saplingPlaced(QcBlockPos pos, QcBlockId sapling);

    void tick(const QcBlockAccess &world, double dt, const LoadedFunction &isLoaded, const SetBlock &setBlock,
              const BreakBlock &breakBlock);

    const QList<Sapling> &saplings() const { return m_saplings; }
    void restoreSaplings(const QList<Sapling> &saplings);
    qsizetype pendingCheckCount() const { return m_pending.size(); }

private:
    struct PendingCheck
    {
        QcBlockPos pos;
        double due = 0.0;
    };

    enum class Support { Trunk, None, Unknown };

    // Unknown when part of the surroundings is not loaded and no trunk was found.
    Support supportOf(QcBlockPos pos, const QcBlockAccess &world) const;
    // Returns false when the tree has no room yet.
    bool grow(const Sapling &sapling, QcBlockId block, const QcBlockAccess &world, const SetBlock &setBlock);

    std::shared_ptr<const QcContentRegistry> m_content;
    quint64 m_seed = 0;
    QRandomGenerator m_random;
    QSet<QcBlockId> m_leaves;
    QSet<QcBlockId> m_trunks;
    // Tree shape per sapling block; blocks whose feature is unknown never grow.
    QHash<QcBlockId, QcTreeShape::Params> m_trees;
    QHash<QcBlockId, QSet<QcBlockId>> m_plantOn;
    QList<PendingCheck> m_pending;
    QSet<QcBlockPos> m_pendingPositions;
    QList<Sapling> m_saplings;
    double m_time = 0.0;
};
