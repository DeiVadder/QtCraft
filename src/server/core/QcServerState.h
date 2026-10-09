#pragma once

#include "QcContainerManager.h"
#include "QcContentRegistry.h"
#include "QcFallingBlockManager.h"
#include "QcItemEntityManager.h"
#include "QcLiquidFlow.h"
#include "QcMachineManager.h"
#include "QcMobManager.h"
#include "QcPlantManager.h"
#include "QcServerClient.h"
#include "QcServerConfig.h"
#include "QcServerWorld.h"
#include "QcWorldStorage.h"

#include <QRandomGenerator>
#include <QSet>

#include <memory>
#include <optional>
#include <vector>

// The world, the clients and the entity managers of the authoritative server, shared by its parts.
// Lives in the server's thread like QcServer itself.
struct QcServerState
{
    QcServerState(QcServerConfig serverConfig, std::shared_ptr<const QcContentRegistry> registry,
                  std::unique_ptr<QcWorldGenerator> generator);

    quint32 newEntityId() { return nextEntityId++; }
    bool isLoadedAt(QcBlockPos pos) const;
    const QcItemDefinition *heldItem(const QcServerClient &client) const;
    bool isWithinReach(const QcServerClient &client, QcBlockPos pos) const;
    // On the generated surface of the spawn column (or the configured one).
    QcVec3 spawnPosition();

    QcServerConfig config;
    std::shared_ptr<const QcContentRegistry> content;
    QcServerWorld world;
    std::vector<std::unique_ptr<QcServerClient>> clients;
    QcWorldInfo info;
    quint64 tickCount = 0;
    quint32 nextEntityId = 1;
    QcMobManager mobs;
    QcItemEntityManager items;
    QcPlantManager plants;
    QcMachineManager machines;
    QcContainerManager containers;
    QcLiquidFlow liquids;
    QcFallingBlockManager falling;
    QRandomGenerator random;
    // Separate from random so that sounds never change drops or spawns.
    QRandomGenerator soundRandom;
    // Entities that died this tick; their removal tells clients to play the death animation.
    QSet<quint32> died;
    // The generator's spawn point, searched once (it may generate chunks to find open ground).
    std::optional<QcBlockPos> generatorSpawn;
};
