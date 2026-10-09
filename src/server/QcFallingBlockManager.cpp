#include "QcFallingBlockManager.h"

#include "QcPhysics.h"

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
    return block && content.isValidBlock(*block) && content.block(*block).falls && below.y >= 0 && isPassable(world, below);
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
    Falling falling;
    falling.id = id;
    falling.block = block;
    falling.motion.position = {from.x + 0.5, double(from.y), from.z + 0.5};
    m_falling.append(falling);
}

QList<QcFallingBlockManager::Landing> QcFallingBlockManager::tick(const QcBlockAccess &world, double dt, const LoadedFunction &isLoaded)
{
    QList<Landing> landed;
    QList<Falling> still;
    still.reserve(m_falling.size());
    for (Falling falling : std::as_const(m_falling)) {
        if (!isLoaded(falling.motion.position)) {
            still.append(falling);
            continue;
        }
        falling.age += dt;
        QcEntityMotion &motion = falling.motion;
        motion.inWater = QcPhysics::isLiquidAt(world, QcPhysics::blockContaining(motion.position + QcVec3{0.0, size * 0.5, 0.0}));
        QcPhysics::stepEntity(motion, {}, 0.0, false, size, size, world, dt);
        if (motion.position.y < -16.0) {
            continue;
        }
        if (!motion.onGround && falling.age < maxSeconds) {
            still.append(falling);
            continue;
        }
        // Resting on something: the cell holding the lower half of the block.
        const QcBlockPos cell = QcPhysics::blockContaining(motion.position + QcVec3{0.0, size * 0.25, 0.0});
        landed.append({falling.block, cell, motion.onGround && cell.isInsideWorldHeight() && isPassable(world, cell)});
    }
    m_falling = std::move(still);
    return landed;
}

void QcFallingBlockManager::restore(const QList<Falling> &falling, const std::function<quint32()> &nextEntityId)
{
    m_falling.clear();
    for (Falling block : falling) {
        if (m_falling.size() >= maxFalling) {
            break;
        }
        block.id = nextEntityId();
        m_falling.append(block);
    }
}
