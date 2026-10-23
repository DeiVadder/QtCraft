#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcEntityMotion.h"
#include "common/world/QcBlockAccess.h"
#include "server/core/QcServerLimits.h"
#include "server/entities/QcFallingBlock.h"

#include <QList>

#include <functional>
#include <memory>
#include <optional>

// Blocks that fall (sand, gravel) while nothing holds them (decision E-055). A falling block is an
// entity with the shared physics; where it comes to rest it becomes its block again if the cell
// is free (air, liquid, replaceable), otherwise it drops as items. Only blocks in loaded chunks
// move. Their number is bounded; beyond it a block drops straight to where it would land.
class QcFallingBlockManager
{
public:
    // Falling blocks at once by default (QcServerLimits::fallingBlocks).
    static constexpr int maxFalling = static_cast<int>(QcServerLimits{}.fallingBlocks);
    // Edge length of the falling cube, a little less than a block so it slides into one-wide shafts.
    static constexpr double size = 0.98;
    // A block still falling after this long (stuck on something) drops as items.
    static constexpr double maxSeconds = 30.0;
    // Scan depth for blocks that land without becoming entities.
    static constexpr int maxDirectDrop = 256;

    struct Landing
    {
        QcBlockId block = qcAirBlockId;
        // The cell the block came to rest in; `placed` when it may become the block there.
        QcBlockPos pos;
        bool placed = false;
        // Blocks it fell (0 when unknown).
        double fallen = 0.0;
    };

    explicit QcFallingBlockManager(std::shared_ptr<const QcContentRegistry> content);

    // Whether a falling block may move into the cell (air, liquid, replaceable blocks).
    static bool isPassable(const QcBlockAccess &world, QcBlockPos pos);
    // Whether the block at `pos` would start falling now (it falls and the cell below is passable;
    // below the lowest layer is the void, decision E-288).
    static bool shouldFall(const QcBlockAccess &world, QcBlockPos pos);
    // The cell a block let go at `from` comes to rest in, scanning down; std::nullopt below the world
    // or in unloaded chunks.
    [[nodiscard]] static std::optional<QcBlockPos> restingCell(const QcBlockAccess &world, QcBlockPos from);

    bool isFull() const { return m_falling.size() >= m_maxFalling; }
    void setMaxFalling(qsizetype limit) { m_maxFalling = limit; }
    // Starts a falling block in the cell `from` (the caller removes the block there).
    void start(QcBlockPos from, QcBlockId block, quint32 id);
    // One tick of physics for the blocks that do not rest (QcEntities::settle); returns the blocks
    // that came to rest. Blocks that fall below QcWorld::voidLimitY disappear, resting ones too
    // (decision E-288).
    QList<Landing> tick(const QcBlockAccess &world, double dt);

    const QList<QcFallingBlock> &all() const { return m_falling; }
    // For moving falling blocks from outside (pistons push them); never add or remove through it.
    QList<QcFallingBlock> &editableAll() { return m_falling; }
    // Removes the blocks falling inside the chunk; returns how many.
    qsizetype removeInChunk(QcChunkPos chunk);
    // Replaces all falling blocks (from a save); ids come from `nextEntityId`.
    void restore(const QList<QcFallingBlock> &falling, const std::function<quint32()> &nextEntityId);
    // The same without clearing first (a chunk came back, decision E-374); returns how many were left
    // out at the limit.
    qsizetype adopt(const QList<QcFallingBlock> &falling, const std::function<quint32()> &nextEntityId);

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QList<QcFallingBlock> m_falling;
    qsizetype m_maxFalling = maxFalling;
};
