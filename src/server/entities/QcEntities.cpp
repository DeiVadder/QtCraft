#include "QcEntities.h"

#include "QcPhysics.h"

#include <algorithm>

QcEntities::QcEntities(const std::shared_ptr<const QcContentRegistry> &content, quint64 seed, const Clients &clients)
    : m_content(content)
    , m_clients(clients)
    , m_creatures(content, seed)
    , m_items(content, seed)
    , m_falling(content)
{
}

void QcEntities::applyLimits(const QcServerLimits &limits)
{
    m_falling.setMaxFalling(qsizetype(limits.fallingBlocks));
    m_items.setLimits(qsizetype(limits.itemsAwake), qsizetype(limits.itemsQueued), qsizetype(limits.itemsTotal));
    m_creatures.setCaps(int(limits.hostileCreaturesPerPlayer), int(limits.passiveCreaturesPerPlayer), int(limits.ambientCreaturesPerPlayer));
    m_stands.setLimit(qsizetype(limits.maxArmourStands));
    m_moving.setMaxMoving(qsizetype(limits.movingBlocksShown));
}

void QcEntities::settle(const ChunkLoadedFunction &isLoaded)
{
    settle(m_creatures.editableMobs(), isLoaded);
    settle(m_falling.editableAll(), isLoaded);
    settle(m_items.editableItems(), isLoaded);
    settle(m_stands.editableStands(), isLoaded);
}

std::optional<QcAabb> QcEntities::boxOf(const QcEntity &entity) const
{
    const QcVec3 &feet = entity.motion.position;
    switch (entity.kind) {
    case QcEntityKind::Player:
        return QcPhysics::boxAt(feet, QcPhysics::playerWidth, QcPhysics::playerHeight);
    case QcEntityKind::Creature: {
        const QList<QcMobDefinition> &definitions = m_content->mobs();
        const int type = static_cast<const QcCreature &>(entity).type;
        if (type < 0 || type >= definitions.size()) {
            return std::nullopt;
        }
        return QcPhysics::boxAt(feet, definitions[type].width, definitions[type].height);
    }
    case QcEntityKind::Item:
        return QcPhysics::boxAt(feet, QcItemEntityManager::itemSize, QcItemEntityManager::itemSize);
    case QcEntityKind::FallingBlock:
        return QcPhysics::boxAt(feet, QcFallingBlockManager::size, QcFallingBlockManager::size);
    case QcEntityKind::ArmourStand:
        return QcArmourStandManager::bodyOf(static_cast<const QcArmourStand &>(entity));
    case QcEntityKind::MovingBlock:
        break;
    }
    return std::nullopt;
}

void QcEntities::displace(QcEntity &entity, const QcVec3 &delta) const
{
    entity.motion.position += delta;
    switch (entity.kind) {
    case QcEntityKind::Player:
        // Like any other server correction (decision E-337).
        static_cast<QcServerClient &>(entity).stateDirty = true;
        break;
    case QcEntityKind::Item:
        static_cast<QcItemEntity &>(entity).resting = false;
        break;
    case QcEntityKind::ArmourStand:
        // Stands send no moves, only spawns.
        markStale(entity.id);
        break;
    default:
        break;
    }
}

bool QcEntities::overlapsLiving(const QList<QcAabb> &boxes) const
{
    // Only the living kinds: no item, stand or block can be trapped by a door or a landing block.
    const auto overlaps = [this, &boxes](const QcEntity &body) {
        const std::optional<QcAabb> box = boxOf(body);
        return box && std::ranges::any_of(boxes, [&box](const QcAabb &other) { return box->intersects(other); });
    };
    return std::ranges::any_of(m_creatures.mobs(), overlaps)
           || std::ranges::any_of(m_clients, [&overlaps](const std::unique_ptr<QcServerClient> &client) { return isPresent(*client) && overlaps(*client); });
}

void QcEntities::markStale(quint32 id) const
{
    for (const std::unique_ptr<QcServerClient> &client : m_clients) {
        if (client->view.known.contains(id)) {
            client->view.stale.insert(id);
        }
    }
}
