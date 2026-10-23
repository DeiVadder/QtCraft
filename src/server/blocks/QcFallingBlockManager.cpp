#include "server/blocks/QcFallingBlockManager.h"

#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"

QcFallingBlockManager::QcFallingBlockManager(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
}

bool QcFallingBlockManager::isPassable(const QcBlockAccess &world, QcBlockPos pos)
{
    if (!pos.isInsideWorldHeight()) {
        return pos.y < 0;
    }

    const std::optional<QcBlockId> block = world.blockAt(pos);
    const QcContentRegistry &content = world.content();
    if (!block || !content.isValidBlock(*block)) {
        return false;
    }

    const QcBlockDefinition &definition = content.block(*block);
    return *block == qcAirBlockId || definition.liquid || definition.replaceable;
}

bool QcFallingBlockManager::shouldFall(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const QcContentRegistry &content = world.content();
    const QcBlockPos below = pos.offset(0, -1, 0);
    return block && content.isValidBlock(*block) && content.block(*block).falls && isPassable(world, below);
}

std::optional<QcBlockPos> QcFallingBlockManager::restingCell(const QcBlockAccess &world, QcBlockPos from)
{
    QcBlockPos cell = from;
    for (int step = 0; step < maxDirectDrop; ++step) {
        const QcBlockPos below = cell.offset(0, -1, 0);
        if (below.y < 0) {
            return std::nullopt;
        }
        if (!world.blockAt(below)) {
            return std::nullopt;
        }
        if (!isPassable(world, below)) {
            return cell;
        }
        cell = below;
    }
    return cell;
}

void QcFallingBlockManager::start(QcBlockPos from, QcBlockId block, quint32 id)
{
    QcFallingBlock falling;
    falling.id = id;
    falling.block = block;
    falling.motion.position = {from.x + 0.5, static_cast<double>(from.y), from.z + 0.5};
    falling.startY = from.y;
    m_falling.append(falling);
}

QList<QcFallingBlockManager::Landing> QcFallingBlockManager::tick(const QcBlockAccess &world, double dt)
{
    QList<Landing> landed;
    QList<QcFallingBlock> still;
    still.reserve(m_falling.size());

    for (QcFallingBlock falling : std::as_const(m_falling)) {
        // Also those resting in unloaded chunks (from a save).
        if (QcWorld::isInVoid(falling.motion.position.y)) {
            continue;
        }
        if (falling.dormant) {
            still.append(falling);
            continue;
        }

        falling.age += dt;
        QcEntityMotion &motion = falling.motion;
        motion.inWater = QcPhysics::isLiquidAt(world, QcPhysics::blockContaining(motion.position + QcVec3{0.0, size * 0.5, 0.0}));
        motion.step(QcPhysics::Steering{}, QcPhysics::BodySize{size, size}, world, dt);

        if (QcWorld::isInVoid(motion.position.y)) {
            continue;
        }

        if (!motion.onGround && falling.age < maxSeconds) {
            still.append(falling);
            continue;
        }

        // Resting on something: the cell holding the lower half of the block.
        const QcBlockPos cell = QcPhysics::blockContaining(motion.position + QcVec3{0.0, size * 0.25, 0.0});
        const double fallen = falling.startY ? std::max(0.0, *falling.startY - motion.position.y) : 0.0;
        landed.append({falling.block, cell, motion.onGround && cell.isInsideWorldHeight() && isPassable(world, cell), fallen});
    }

    m_falling = std::move(still);
    return landed;
}

qsizetype QcFallingBlockManager::removeInChunk(QcChunkPos chunk)
{
    return m_falling.removeIf([chunk](const QcFallingBlock &falling) { return QcPhysics::blockContaining(falling.motion.position).chunk() == chunk; });
}

void QcFallingBlockManager::restore(const QList<QcFallingBlock> &falling, const std::function<quint32()> &nextEntityId)
{
    m_falling.clear();
    adopt(falling, nextEntityId);
}

qsizetype QcFallingBlockManager::adopt(const QList<QcFallingBlock> &falling, const std::function<quint32()> &nextEntityId)
{
    qsizetype left = 0;
    for (QcFallingBlock block : falling) {
        if (m_falling.size() >= m_maxFalling) {
            ++left;
            continue;
        }
        block.id = nextEntityId();
        block.settled = false;
        m_falling.append(block);
    }
    return left;
}
