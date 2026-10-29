// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcBlockAccess.h"
#include "server/blockentities/QcBlockEntities.h"
#include "server/core/QcServerLimits.h"
#include "worldgen/terrain/QcTreeShape.h"

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
// while their chunk is loaded; their timers are block entities (QcBlockEntities::Sapling).
class QcPlantManager
{
public:
    // Leaves stay as long as a trunk block lies within this many blocks along every axis.
    static constexpr int leafReach = 4;
    static constexpr double minDecayDelay = 0.5;
    static constexpr double maxDecayDelay = 4.0;
    // Leaves decaying and checked per tick and checks waiting at most, by default (QcServerLimits
    // leafDecaysPerTick, leafChecksPerTick, leafPendingChecks).
    static constexpr int maxDecaysPerTick = static_cast<int>(QcServerLimits{}.leafDecaysPerTick);
    static constexpr int maxChecksPerTick = static_cast<int>(QcServerLimits{}.leafChecksPerTick);
    // Leaves next to unloaded chunks are checked again after this many seconds.
    static constexpr double recheckSeconds = 5.0;
    static constexpr int maxPendingChecks = static_cast<int>(QcServerLimits{}.leafPendingChecks);
    // A sapling without room to grow tries again after this many seconds.
    static constexpr double retrySeconds = 30.0;

    // Sets a block and tells the clients; returns false when it could not be set.
    using SetBlock = std::function<bool(QcBlockPos pos, QcBlockId block)>;
    // Removes a block as if it broke by itself, dropping its items.
    using BreakBlock = std::function<void(QcBlockPos pos)>;
    using LoadedFunction = std::function<bool(QcBlockPos pos)>;
    // What a tick may ask of the world around the plants.
    struct Actions
    {
        LoadedFunction isLoaded;
        SetBlock setBlock;
        BreakBlock breakBlock;
    };

    QcPlantManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed);

    bool isLeaves(QcBlockId block) const { return m_leaves.contains(block); }
    bool isTrunk(QcBlockId block) const { return m_trunks.contains(block); }
    // Whether `sapling` may stand on `ground`; false for blocks that are not saplings.
    bool canPlant(QcBlockId sapling, std::optional<QcBlockId> ground) const;

    // Tell the manager that the block `previous` at `pos` was removed.
    void blockRemoved(QcBlockPos pos, QcBlockId previous, const QcBlockAccess &world);
    // Starts the growth timer of a sapling block just placed at `pos` (a random time of its
    // definition) in `entities`.
    void saplingPlaced(QcBlockEntities &entities, QcBlockPos pos, QcBlockId sapling);

    // Decays leaves and grows the saplings of `entities` in the order they were planted.
    void tick(const QcBlockAccess &world, QcBlockEntities &entities, double dt, const Actions &actions);

    qsizetype pendingCheckCount() const { return m_pending.size(); }
    void setLeafLimits(qint64 decays, qint64 checks, qint64 pending);

private:
    // Counts every state of `block` as a trunk that keeps leaves.
    void addTrunk(QcBlockId block);
    struct PendingCheck
    {
        QcBlockPos pos;
        double due = 0.0;
    };

    enum class Support { Trunk, None, Unknown };

    // Unknown when part of the surroundings is not loaded and no trunk was found.
    Support supportOf(QcBlockPos pos, const QcBlockAccess &world) const;
    // Support of the row of leafReach blocks either side of `centre` along z: a trunk in it, else
    // unknown when cells of it are not loaded.
    [[nodiscard]] Support rowSupport(QcBlockPos centre, const QcBlockAccess &world) const;
    // Plans a decay check for leaves at `pos` (nothing for other blocks or when already planned).
    void planDecayCheck(QcBlockPos pos, const QcBlockAccess &world);
    void decayLeaves(const QcBlockAccess &world, const BreakBlock &breakBlock);
    void growSaplings(const QcBlockAccess &world, QcBlockEntities &entities, double dt, const LoadedFunction &isLoaded,
                      const SetBlock &setBlock);
    // Grows the sapling at `sapling` (with three more in a square into the giant tree); returns false
    // when the tree has no room yet.
    bool grow(QcBlockPos sapling, QcBlockId block, const QcBlockAccess &world, const SetBlock &setBlock);
    // The north-west corner of a 2×2 square of `block` saplings that holds `sapling`, if any.
    [[nodiscard]] std::optional<QcBlockPos> giantSquare(QcBlockPos sapling, QcBlockId block, const QcBlockAccess &world) const;
    // Grows `tree` on `base` where the `saplings` stand; false without room.
    bool growTree(const QcTreeShape::Params &tree, QcBlockPos base, const QList<QcBlockPos> &saplings, const QcBlockAccess &world,
                  const SetBlock &setBlock);

    std::shared_ptr<const QcContentRegistry> m_content;
    quint64 m_seed = 0;
    QRandomGenerator m_random;
    QSet<QcBlockId> m_leaves;
    QSet<QcBlockId> m_trunks;
    // Tree shape per sapling block; blocks whose feature is unknown never grow.
    QHash<QcBlockId, QcTreeShape::Params> m_trees;
    // Giant tree per sapling block that grows one from a 2×2 square (decision E-822).
    QHash<QcBlockId, QcTreeShape::Params> m_giantTrees;
    QHash<QcBlockId, QSet<QcBlockId>> m_plantOn;
    QList<PendingCheck> m_pending;
    QSet<QcBlockPos> m_pendingPositions;
    qint64 m_decaysPerTick = maxDecaysPerTick;
    qint64 m_checksPerTick = maxChecksPerTick;
    qint64 m_pendingLimit = maxPendingChecks;
    double m_time = 0.0;
};
