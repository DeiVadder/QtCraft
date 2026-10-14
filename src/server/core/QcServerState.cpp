#include "QcServerState.h"

#include "QcLineOfSight.h"
#include "QcPhysics.h"
#include "QcRaycast.h"
#include "QcWorldConstants.h"

#include <algorithm>

#include <utility>

namespace {

bool seenFromEyes(const QcServerState &state, const QcServerClient &client, const QcAabb &box, std::optional<QcBlockPos> target)
{
    const QcVec3 eyeOffset{0.0, QcPhysics::playerEyeHeight, 0.0};
    if (QcLineOfSight::reaches(state.world, client.motion.position + eyeOffset, box, target)) {
        return true;
    }
    return client.previousPosition && QcLineOfSight::reaches(state.world, *client.previousPosition + eyeOffset, box, target);
}

} // namespace

QcServerState::QcServerState(QcServerConfig serverConfig, std::shared_ptr<const QcContentRegistry> registry,
                             std::unique_ptr<QcWorldGenerator> generator)
    : config(std::move(serverConfig))
    , content(registry)
    , world(std::move(registry), std::move(generator))
    , mobs(content, quint64(config.seed))
    , items(content, quint64(config.seed))
    , plants(content, quint64(config.seed))
    , growth(content, quint64(config.seed))
    , machineRules(content)
    , liquids(content)
    , falling(content)
    , random(quint32(quint64(config.seed) ^ 0xD2095u))
    , soundRandom(quint32(quint64(config.seed) ^ 0x50D5u))
{
    applyLimits();
}

void QcServerState::applyLimits()
{
    circuits.maxPendingChanges = qsizetype(config.limits.signalMaxPendingChanges);
    hoppers.cooldownTicks = int(config.limits.hopperCooldownTicks);
    const QcServerLimits &limits = config.limits;
    world.blockEntities().setLimits({qsizetype(limits.maxContainers), qsizetype(limits.maxMachines), qsizetype(limits.maxSaplings)});
    liquids.setMaxPlanned(qsizetype(limits.liquidPlannedSteps));
    falling.setMaxFalling(qsizetype(limits.fallingBlocks));
    items.setLimits(qsizetype(limits.itemsAwake), qsizetype(limits.itemsQueued), qsizetype(limits.itemsTotal));
    mobs.setCaps(int(limits.hostileCreaturesPerPlayer), int(limits.passiveCreaturesPerPlayer), int(limits.ambientCreaturesPerPlayer));
    growth.setRandomTicksPerSection(int(limits.randomTicksPerSection));
    plants.setLeafLimits(limits.leafDecaysPerTick, limits.leafChecksPerTick, limits.leafPendingChecks);
    stands.setLimit(qsizetype(limits.maxArmourStands));
    movingBlocks.setMaxMoving(qsizetype(limits.movingBlocksShown));
}

QcPermission QcServerState::permissionOf(const QcServerClient &client) const
{
    return client.local || access.isOperator(client.name, client.fingerprint) ? QcPermission::Operator : QcPermission::Player;
}

bool QcServerState::isLoadedAt(QcBlockPos pos) const
{
    return world.chunk(pos.chunk()) != nullptr;
}

const QcItemDefinition *QcServerState::heldItem(const QcServerClient &client) const
{
    const QcItemStack &held = client.inventory.selectedStack();
    return !held.isEmpty() && content->isValidItem(held.item) ? &content->item(held.item) : nullptr;
}

bool QcServerState::isWithinReach(const QcServerClient &client, QcBlockPos pos) const
{
    // One metre of slack for movement between the client's decision and the server's check.
    constexpr double tolerance = 1.0;
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
    return (centre - eye).length() <= QcRaycast::playerReach + tolerance;
}

bool QcServerState::canSee(const QcServerClient &client, QcBlockPos pos) const
{
    return seenFromEyes(*this, client, QcLineOfSight::blockBox(pos), pos);
}

bool QcServerState::canSee(const QcServerClient &client, const QcAabb &body) const
{
    return seenFromEyes(*this, client, body, std::nullopt);
}

QcVec3 QcServerState::spawnPosition()
{
    if (!generatorSpawn) {
        generatorSpawn = world.generator().spawnPoint();
    }

    QcBlockPos spawn = *generatorSpawn;
    if (config.spawnColumn) {
        spawn = {(*config.spawnColumn)[0], 0, (*config.spawnColumn)[1]};
    }

    if (!config.spawnColumn) {
        if (const std::optional<QcBlockPos> ground = naturalGroundNear(spawn)) {
            return {ground->x + 0.5, double(ground->y + 1), ground->z + 0.5};
        }
    }

    // Stand on whatever the stored column contains (the test hook's column, or no ground nearby).
    const std::shared_ptr<const QcChunk> spawnChunk = world.loadChunk(spawn.chunk());
    spawn.y = spawnChunk->highestNonAir(spawn.localX(), spawn.localZ()) + 1;

    // An empty column (a void world without platform, a cleared spawn) would drop the player into
    // the void again and again.
    if (spawn.y == 0) {
        const int spawnHeight = generatorSpawn.value_or(QcBlockPos{}).y;
        const QcBlockPos ground{spawn.x, std::clamp(spawnHeight - 1, 0, QcWorld::worldHeight - 3), spawn.z};
        if (placeSpawnGround(ground)) {
            spawn.y = ground.y + 1;
        }
    }
    return {spawn.x + 0.5, double(spawn.y), spawn.z + 0.5};
}

bool QcServerState::placeSpawnGround(QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.generator().platformBlock().or_else([this] {
        return content->blockId(QStringLiteral("qtcraft:stone"));
    });
    if (!block || !world.setBlock(pos, *block)) {
        return false;
    }

    for (const auto &client : clients) {
        if (client->state == QcServerClient::State::Playing && client->sentChunks.contains(pos.chunk())) {
            client->connection->send(QcMsg::BlockUpdate{pos, *block});
        }
    }
    return true;
}

std::optional<QcBlockPos> QcServerState::naturalGroundNear(QcBlockPos column)
{
    for (int radius = 0; radius <= spawnGroundRadius; ++radius) {
        if (const std::optional<QcBlockPos> ground = naturalGroundOnRing(column, radius)) {
            return ground;
        }
    }
    return std::nullopt;
}

std::optional<QcBlockPos> QcServerState::naturalGroundOnRing(QcBlockPos column, int radius)
{
    // Each side of the square ring is walked once (corners twice, which is harmless).
    for (int i = -radius; i <= radius; ++i) {
        const std::pair<int, int> sides[] = {{i, -radius}, {i, radius}, {-radius, i}, {radius, i}};
        for (const auto [dx, dz] : sides) {
            if (const std::optional<QcBlockPos> ground = naturalGroundAt(column.x + dx, column.z + dz)) {
                return ground;
            }
        }
    }
    return std::nullopt;
}

std::optional<QcBlockPos> QcServerState::naturalGroundAt(int x, int z)
{
    QcBlockPos top{x, 0, z};
    const std::shared_ptr<const QcChunk> chunk = world.loadChunk(top.chunk());
    top.y = chunk->highestNonAir(top.localX(), top.localZ());
    if (top.y < 0 || !world.generator().isNaturalGround(chunk->block(top.localX(), top.y, top.localZ()))) {
        return std::nullopt;
    }
    return top;
}

void QcServerState::markStale(quint32 entityId)
{
    for (const auto &client : clients) {
        if (client->knownEntities.contains(entityId)) {
            client->staleEntities.insert(entityId);
        }
    }
}
