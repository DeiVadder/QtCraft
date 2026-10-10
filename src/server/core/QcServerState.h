#pragma once

#include "QcContainerManager.h"
#include "QcContentRegistry.h"
#include "QcFallingBlockManager.h"
#include "QcItemEntityManager.h"
#include "QcLiquidFlow.h"
#include "QcMachineManager.h"
#include "QcMobManager.h"
#include "QcPlantGrowth.h"
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

    // Line of sight from the player's eye, now or before the last input (decision E-092), to a
    // block (which does not hide itself) or to a body.
    bool canSee(const QcServerClient &client, QcBlockPos pos) const;
    bool canSee(const QcServerClient &client, const QcAabb &body) const;
    // On the surface of the spawn column (or the configured one) as the world stores it; when that
    // is no natural ground (a tree, a building, water), on the nearest natural ground around it.
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
    QcPlantGrowth growth;
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

private:
    // Searched by spawnPosition() on squares of growing radius around the spawn column.
    static constexpr int spawnGroundRadius = 16;
    // Nearest stored column around `column` (within spawnGroundRadius) whose top block is natural
    // ground; returns that top block.
    std::optional<QcBlockPos> naturalGroundNear(QcBlockPos column);
    // The same on the square ring of `radius` around `column`.
    std::optional<QcBlockPos> naturalGroundOnRing(QcBlockPos column, int radius);
    // The top block of a stored column if it is natural ground, else nullopt.
    std::optional<QcBlockPos> naturalGroundAt(int x, int z);
};
