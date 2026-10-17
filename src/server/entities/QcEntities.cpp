#include "QcEntities.h"

#include "QcPhysics.h"

#include <algorithm>

QcEntities::QcEntities(const std::shared_ptr<const QcContentRegistry> &content, quint64 seed, const Clients &clients)
    : m_content(content)
    , m_clients(clients)
    , m_creatures(content, seed)
    , m_items(content, seed)
    , m_falling(content)
    , m_carts(*content)
    , m_orbs(seed)
{
}

void QcEntities::applyLimits(const QcServerLimits &limits)
{
    m_falling.setMaxFalling(qsizetype(limits.fallingBlocks));
    m_items.setLimits(qsizetype(limits.itemsAwake), qsizetype(limits.itemsQueued), qsizetype(limits.itemsTotal));
    m_creatures.setCaps(int(limits.hostileCreaturesPerPlayer), int(limits.passiveCreaturesPerPlayer), int(limits.ambientCreaturesPerPlayer));
    m_creatures.setPathLimits(limits.pathNodesPerTick, limits.pathNodesPerSearch, limits.pathLength);
    m_creatures.setCrowdLimits(limits.crowdThrottled, limits.crowdIdle);
    m_creatures.setVillagerLimits({qsizetype(limits.villagersPerVillage), qsizetype(limits.villagers)},
                                  {int(limits.villagerThoughtsPerTick), int(limits.villagerRoutesPerTick), int(limits.villagerHarvestsPerTick)});
    m_creatures.setGolemLimit(limits.villageGolems);
    m_stands.setLimit(qsizetype(limits.maxArmourStands));
    m_moving.setMaxMoving(qsizetype(limits.movingBlocksShown));
    m_carts.setLimit(qsizetype(limits.maxMinecarts));
    m_orbs.setLimit(qsizetype(limits.experienceOrbs));
}

namespace {

// Looks up whether the entity's chunk is loaded when it is new, moved to another chunk or the loaded
// chunks changed (`everyone`); notes the chunk of a resting one.
void settleOne(QcEntity &entity, const QcEntities::ChunkLoadedFunction &isLoaded, bool everyone, QSet<QcChunkPos> &strays)
{
    const QcChunkPos chunk = QcEntities::chunkOf(entity);
    if (!everyone && entity.settled && entity.settledChunk == chunk) {
        return;
    }
    entity.dormant = !isLoaded(chunk);
    entity.settledChunk = chunk;
    entity.settled = true;
    if (entity.dormant) {
        strays.insert(chunk);
    }
}

} // namespace

QSet<QcChunkPos> QcEntities::settle(const ChunkLoadedFunction &isLoaded, quint64 loadedEpoch)
{
    const bool everyone = m_settledEpoch != loadedEpoch;
    m_settledEpoch = loadedEpoch;
    QSet<QcChunkPos> strays;
    const auto settleList = [&isLoaded, &strays, everyone](auto &list) {
        for (QcEntity &entity : list) {
            settleOne(entity, isLoaded, everyone, strays);
        }
    };
    settleList(m_creatures.editableMobs());
    settleList(m_falling.editableAll());
    settleList(m_items.editableItems());
    settleList(m_stands.editableStands());
    settleList(m_carts.editableAll());
    settleList(m_orbs.editableOrbs());
    return strays;
}

std::optional<QcAabb> QcEntities::boxOf(const QcEntity &entity) const
{
    const QcVec3 &feet = entity.motion.position;
    switch (entity.kind) {
    case QcEntityKind::Player:
        return QcPhysics::boxAt(feet, QcPhysics::playerWidth, QcPhysics::playerHeight);
    case QcEntityKind::Creature: {
        const QList<QcMobDefinition> &definitions = m_content->mobs();
        const QcCreature &creature = entity.as<QcCreature>();
        if (creature.type < 0 || creature.type >= definitions.size()) {
            return std::nullopt;
        }
        const QcMobDefinition &definition = definitions[creature.type];
        // A villager asleep lies in its bed (decision E-411).
        return creature.villager.sleeping ? QcPhysics::lyingBoxAt(feet, creature.yaw, definition.width, definition.height)
                                          : QcPhysics::boxAt(feet, definition.width, definition.height);
    }
    case QcEntityKind::Item:
        return QcPhysics::boxAt(feet, QcItemEntityManager::itemSize, QcItemEntityManager::itemSize);
    case QcEntityKind::FallingBlock:
        return QcPhysics::boxAt(feet, QcFallingBlockManager::size, QcFallingBlockManager::size);
    case QcEntityKind::ArmourStand:
        return QcArmourStandManager::bodyOf(entity.as<QcArmourStand>());
    case QcEntityKind::Minecart:
        return QcMinecartManager::bodyOf(entity.as<QcMinecart>());
    case QcEntityKind::ExperienceOrb:
        return QcPhysics::boxAt(feet, QcExperienceOrbManager::size, QcExperienceOrbManager::size);
    case QcEntityKind::MovingBlock:
        break;
    }
    return std::nullopt;
}

void QcEntities::displace(QcEntity &entity, const QcVec3 &delta) const
{
    if (entity.kind == QcEntityKind::Player && m_carts.ridden(entity.id)) {
        return;
    }

    entity.motion.position += delta;
    switch (entity.kind) {
    case QcEntityKind::Player:
        // Like any other server correction (decision E-337).
        entity.as<QcServerClient>().stateDirty = true;
        break;
    case QcEntityKind::Item:
        entity.as<QcItemEntity>().resting = false;
        break;
    case QcEntityKind::ExperienceOrb:
        entity.as<QcExperienceOrb>().resting = false;
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
