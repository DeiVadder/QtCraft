// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/entities/QcEntities.h"

#include "common/physics/QcPhysics.h"
#include "common/physics/QcPose.h"
#include "server/spawning/QcNaturalSpawner.h"
#include "server/villages/QcVillagePopulation.h"

#include <algorithm>

QcEntities::QcEntities(const std::shared_ptr<const QcContentRegistry> &content, quint64 seed, const Clients &clients)
    : m_content(content)
    , m_clients(clients)
    , m_creatures(content, seed)
    , m_items(content, seed)
    , m_falling(content)
    , m_carts(*content)
    , m_orbs(seed)
    , m_projectiles(content)
{
}

void QcEntities::applyLimits(const QcServerLimits &limits)
{
    m_falling.setMaxFalling(static_cast<qsizetype>(limits.fallingBlocks));
    m_items.setLimits(static_cast<qsizetype>(limits.itemsAwake), static_cast<qsizetype>(limits.itemsQueued), static_cast<qsizetype>(limits.itemsTotal));
    m_creatures.spawner().setCaps(static_cast<int>(limits.monstersPer289Chunks), static_cast<int>(limits.passiveCreaturesPerPlayer),
        static_cast<int>(limits.ambientCreaturesPerPlayer));
    m_creatures.spawner().setWaterCap(static_cast<int>(limits.waterCreaturesPerPlayer));
    m_creatures.setPathLimits(limits.pathNodesPerTick, limits.pathNodesPerSearch, limits.pathLength);
    m_creatures.setCrowdLimits(limits.crowdThrottled, limits.crowdIdle);
    m_creatures.population().setVillagerLimits({static_cast<qsizetype>(limits.villagersPerVillage), static_cast<qsizetype>(limits.villagers)},
                                  {static_cast<int>(limits.villagerThoughtsPerTick), static_cast<int>(limits.villagerRoutesPerTick),
                                  static_cast<int>(limits.villagerHarvestsPerTick)});
    m_creatures.population().setGolemLimit(limits.villageGolems);
    m_stands.setLimit(static_cast<qsizetype>(limits.maxArmourStands));
    m_hangings.setLimit(static_cast<qsizetype>(limits.maxHangings));
    m_moving.setMaxMoving(static_cast<qsizetype>(limits.movingBlocksShown));
    m_carts.setLimit(static_cast<qsizetype>(limits.maxMinecarts));
    boats().setLimit(static_cast<qsizetype>(limits.maxBoats));
    m_orbs.setLimit(static_cast<qsizetype>(limits.experienceOrbs));
    m_projectiles.setLimit(static_cast<qsizetype>(limits.projectiles));
    m_projectiles.setPerChunk(static_cast<qsizetype>(limits.projectilesPerChunk));
    m_explosives.setLimit(static_cast<qsizetype>(limits.primedExplosives));
}

namespace {

// Looks up whether the entity's chunk is loaded when it is new, moved to another chunk or the loaded
// chunks changed (`everyone`); the chunk of one that rests there, which is not loaded.
[[nodiscard]] std::optional<QcChunkPos> settleOne(QcEntity &entity, const QcEntities::ChunkLoadedFunction &isLoaded, bool everyone)
{
    const QcChunkPos chunk = QcEntities::chunkOf(entity);
    if (!everyone && entity.settled && entity.settledChunk == chunk) {
        return std::nullopt;
    }
    entity.dormant = !isLoaded(chunk);
    entity.settledChunk = chunk;
    entity.settled = true;
    return entity.dormant ? std::optional(chunk) : std::nullopt;
}

} // namespace

QSet<QcChunkPos> QcEntities::settle(const ChunkLoadedFunction &isLoaded, quint64 loadedEpoch)
{
    const bool everyone = m_settledEpoch != loadedEpoch;
    m_settledEpoch = loadedEpoch;
    QSet<QcChunkPos> strays;
    const auto settleList = [&isLoaded, &strays, everyone](auto &list) {
        for (QcEntity &entity : list) {
            if (const std::optional<QcChunkPos> stray = settleOne(entity, isLoaded, everyone)) {
                strays.insert(*stray);
            }
        }
    };
    settleList(m_creatures.editableMobs());
    settleList(m_falling.editableAll());
    settleList(m_items.editableItems());
    settleList(m_stands.editableStands());
    settleList(m_carts.editableAll());
    settleList(m_orbs.editableOrbs());
    settleList(m_projectiles.editableAll());
    settleList(boats().editableAll());
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
        // A villager asleep lies in its bed (decisions E-411, E-423).
        return QcPoses::boxAt(creature.pose, feet, creature.yaw, definition.width, definition.height);
    }
    case QcEntityKind::Item:
        return QcPhysics::boxAt(feet, QcItemEntityManager::itemSize, QcItemEntityManager::itemSize);
    case QcEntityKind::FallingBlock:
        return QcPhysics::boxAt(feet, QcFallingBlockManager::size, QcFallingBlockManager::size);
    case QcEntityKind::PrimedExplosive:
        return QcPhysics::boxAt(feet, QcPrimedExplosives::size, QcPrimedExplosives::size);
    case QcEntityKind::ArmourStand:
        return QcArmourStandManager::bodyOf(entity.as<QcArmourStand>());
    case QcEntityKind::Minecart:
        return QcMinecartManager::bodyOf(entity.as<QcMinecart>());
    case QcEntityKind::Boat:
        return QcBoatManager::bodyOf(entity.as<QcBoat>());
    case QcEntityKind::ExperienceOrb:
        return QcPhysics::boxAt(feet, QcExperienceOrbManager::size, QcExperienceOrbManager::size);
    case QcEntityKind::Projectile: {
        // Its position is its centre.
        const QcProjectileDefinition *definition = m_content->projectile(entity.as<QcProjectile>().type);
        const double half = definition ? definition->size * 0.5 : 0.25;
        return QcAabb{feet - QcVec3{half, half, half}, feet + QcVec3{half, half, half}};
    }
    case QcEntityKind::MovingBlock:
    case QcEntityKind::Hanging:
        // Frames and paintings are no bodies: nothing pushes them, they fall off when their wall
        // goes (QcServerHanging::tick).
        break;
    }
    return std::nullopt;
}

void QcEntities::displace(QcEntity &entity, const QcVec3 &delta) const
{
    if (entity.kind == QcEntityKind::Player && (m_carts.ridden(entity.id) || boats().drivenBy(entity.id))) {
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
