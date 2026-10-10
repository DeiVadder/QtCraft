#include "QcServerState.h"

#include "QcLineOfSight.h"
#include "QcPhysics.h"
#include "QcRaycast.h"

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
    , machines(content)
    , containers(content)
    , liquids(content)
    , falling(content)
    , random(quint32(quint64(config.seed) ^ 0xD2095u))
    , soundRandom(quint32(quint64(config.seed) ^ 0x50D5u))
{
}

QcPermission QcServerState::permissionOf(const QcServerClient &client) const
{
    return client.local || access.isOperator(client.name) ? QcPermission::Operator : QcPermission::Player;
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
    return {spawn.x + 0.5, double(spawn.y), spawn.z + 0.5};
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
