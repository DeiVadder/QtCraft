#pragma once

#include "QcBlockAccess.h"
#include "QcContentRegistry.h"
#include "QcEntityMotion.h"

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
    static constexpr int maxFalling = 2048;
    // Edge length of the falling cube, a little less than a block so it slides into one-wide shafts.
    static constexpr double size = 0.98;
    // A block still falling after this long (stuck on something) drops as items.
    static constexpr double maxSeconds = 30.0;
    // Scan depth for blocks that land without becoming entities.
    static constexpr int maxDirectDrop = 256;

    struct Falling
    {
        quint32 id = 0;
        QcBlockId block = qcAirBlockId;
        QcEntityMotion motion;
        double age = 0.0;
    };

    struct Landing
    {
        QcBlockId block = qcAirBlockId;
        // The cell the block came to rest in; `placed` when it may become the block there.
        QcBlockPos pos;
        bool placed = false;
    };

    using LoadedFunction = std::function<bool(const QcVec3 &position)>;

    explicit QcFallingBlockManager(std::shared_ptr<const QcContentRegistry> content);

    // Whether a falling block may move into the cell (air, liquid, replaceable blocks).
    static bool isPassable(const QcBlockAccess &world, QcBlockPos pos);
    // Whether the block at `pos` would start falling now (it falls and the cell below is passable).
    static bool shouldFall(const QcBlockAccess &world, QcBlockPos pos);
    // The cell a block let go at `from` comes to rest in, scanning down; std::nullopt below the world
    // or in unloaded chunks.
    static std::optional<QcBlockPos> restingCell(const QcBlockAccess &world, QcBlockPos from);

    bool isFull() const { return m_falling.size() >= maxFalling; }
    // Starts a falling block in the cell `from` (the caller removes the block there).
    void start(QcBlockPos from, QcBlockId block, quint32 id);
    // One tick of physics; returns the blocks that came to rest. Blocks that fall out of the world
    // disappear.
    QList<Landing> tick(const QcBlockAccess &world, double dt, const LoadedFunction &isLoaded);

    const QList<Falling> &all() const { return m_falling; }
    // Replaces all falling blocks (from a save); ids come from `nextEntityId`.
    void restore(const QList<Falling> &falling, const std::function<quint32()> &nextEntityId);

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QList<Falling> m_falling;
};
