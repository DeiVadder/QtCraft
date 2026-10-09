#include "QcServerState.h"

#include "QcPhysics.h"
#include "QcRaycast.h"

QcServerState::QcServerState(QcServerConfig serverConfig, std::shared_ptr<const QcContentRegistry> registry,
                             std::unique_ptr<QcWorldGenerator> generator)
    : config(std::move(serverConfig))
    , content(registry)
    , world(std::move(registry), std::move(generator))
    , mobs(content, quint64(config.seed))
    , items(content, quint64(config.seed))
    , plants(content, quint64(config.seed))
    , machines(content)
    , containers(content)
    , liquids(content)
    , falling(content)
    , random(quint32(quint64(config.seed) ^ 0xD2095u))
    , soundRandom(quint32(quint64(config.seed) ^ 0x50D5u))
{
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

QcVec3 QcServerState::spawnPosition()
{
    if (!generatorSpawn) {
        generatorSpawn = world.generator().spawnPoint();
    }

    QcBlockPos spawn = *generatorSpawn;
    if (config.spawnColumn) {
        spawn = {(*config.spawnColumn)[0], 0, (*config.spawnColumn)[1]};
    }

    // Stand on whatever the generated column really contains (caves, trees, water surface).
    const std::shared_ptr<const QcChunk> spawnChunk = world.loadChunk(spawn.chunk());
    spawn.y = spawnChunk->highestNonAir(spawn.localX(), spawn.localZ()) + 1;
    return {spawn.x + 0.5, double(spawn.y), spawn.z + 0.5};
}
