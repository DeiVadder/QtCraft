// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "server/advancements/QcServerAdvancements.h"
#include "server/blocks/QcLiquidFlow.h"
#include "server/commands/QcPermission.h"
#include "server/core/QcContactDamage.h"
#include "server/core/QcServerClient.h"
#include "server/core/QcServerConfig.h"
#include "server/core/QcServerWorld.h"
#include "server/core/QcSpawnPoint.h"
#include "server/entities/QcEntities.h"
#include "server/login/QcAccessList.h"
#include "server/login/QcIdentityStore.h"
#include "server/machines/QcMachineRules.h"
#include "server/maps/QcMapStore.h"
#include "server/plants/QcPlantGrowth.h"
#include "server/plants/QcPlantManager.h"
#include "server/signal/QcCircuitState.h"
#include "server/signal/QcHopperQueue.h"
#include "server/storage/QcWorldStorage.h"
#include "server/tick/QcServerClock.h"

#include <QRandomGenerator>
#include <QSet>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

// The world, the clients and the entities of the authoritative server, shared by its parts
// (decision E-367): each part is reached through its own accessor and keeps its data behind its own
// interface; what changes at the level of the whole server (the clock, the player list) changes
// through a named function. Lives in the server's thread like QcServer itself.
class QcServerState
{
public:
    QcServerState(QcServerConfig serverConfig, std::shared_ptr<const QcContentRegistry> registry,
                  std::unique_ptr<QcWorldGenerator> generator);
    // The parts keep references into it (QcEntities to the clients, QcSpawnPoint to the world).
    QcServerState(const QcServerState &) = delete;
    QcServerState &operator=(const QcServerState &) = delete;
    ~QcServerState() = default;

    bool isLoadedAt(QcBlockPos pos) const;
    const QcItemDefinition *heldItem(const QcServerClient &client) const;
    // The local player (host of a single player world) and listed operators are operators
    // (decision E-243).
    QcPermission permissionOf(const QcServerClient &client) const;
    // The contents of the container or machine at `pos` changed (a screen, a hopper, a furnace):
    // comparators reading it and hoppers next to it look again (decisions E-343, E-344).
    void noteContents(QcBlockPos pos)
    {
        m_hoppers.touched.insert(pos);
        m_circuits.noteContents(m_world, pos);
    }
    // State that belongs to single blocks (decision E-357).
    QcBlockEntities &blockEntities() { return m_world.blockEntities(); }
    const QcBlockEntities &blockEntities() const { return m_world.blockEntities(); }
    // Hands the limits of the config to the parts that keep their own copy (decision E-349).
    void applyLimits();

    // The server's settings; a single-player world's limits replace those of the config (world.json).
    QcServerConfig &config() { return m_config; }
    const QcServerConfig &config() const { return m_config; }
    const QcContentRegistry &content() const { return *m_content; }
    const std::shared_ptr<const QcContentRegistry> &contentPointer() const { return m_content; }
    QcServerWorld &world() { return m_world; }
    const QcServerWorld &world() const { return m_world; }
    // The connected clients; the parts may change a client, only QcServer adds and removes them.
    const std::vector<std::unique_ptr<QcServerClient>> &clients() const { return m_clients; }
    // Every entity of the world (decision E-364); players are the clients.
    QcEntities &entities() { return m_entities; }
    const QcEntities &entities() const { return m_entities; }
    QcSpawnPoint &spawnPoint() { return m_spawnPoint; }
    // Damage of the blocks bodies touch (lava, magma).
    const QcContactDamage &contactDamage() const { return m_contactDamage; }
    // world.json: name, seed, time of day, game rules.
    QcWorldInfo &info() { return m_info; }
    const QcWorldInfo &info() const { return m_info; }
    QcAccessList &access() { return m_access; }
    const QcAccessList &access() const { return m_access; }
    // Identities of remote players per name (decision E-252).
    QcIdentityStore &identities() { return m_identities; }
    // Read by every part; only QcServer advances it, once per tick.
    const QcServerClock &clock() const { return m_clock; }
    QcPlantManager &plants() { return m_plants; }
    const QcPlantManager &plants() const { return m_plants; }
    QcPlantGrowth &growth() { return m_growth; }
    const QcMachineRules &machineRules() const { return m_machineRules; }
    QcLiquidFlow &liquids() { return m_liquids; }
    const QcLiquidFlow &liquids() const { return m_liquids; }
    // Signal Dust circuits (decision E-335) and the hoppers to look at (decision E-344).
    QcCircuitState &circuits() { return m_circuits; }
    const QcCircuitState &circuits() const { return m_circuits; }
    QcHopperQueue &hoppers() { return m_hoppers; }
    // The maps of the world by map id (decision E-799).
    QcMapStore &maps() { return m_maps; }
    const QcMapStore &maps() const { return m_maps; }
    // The adventure guide (decision E-923): the gameplay reports events to it.
    QcServerAdvancements &advancements() { return m_advancements; }
    QRandomGenerator &random() { return m_random; }
    // Separate from random() so that sounds never change drops or spawns.
    QRandomGenerator &soundRandom() { return m_soundRandom; }

    // Someone joined or left, or operator rights changed: the clients get the player list again.
    void notePlayerListChanged() { m_playerListChanged = true; }
    static constexpr qint64 latencyListIntervalNs = 2'000'000'000;
    // A client reported its round trip (decision E-886). A change that shows in the list
    // (QcListedLatency::changed) is listed and the list goes out again, at most once per
    // latencyListIntervalNs of the players' time; smaller changes are ignored.
    void reportLatency(QcServerClient &client, qint32 reportedUs);
    // Whether the list changed since the last call, or round trips changed and their interval passed.
    bool takePlayerListChanged();

private:
    friend class QcServer;
    void advanceClock(qint64 realNs) { m_clock.advance(realNs); }
    std::vector<std::unique_ptr<QcServerClient>> &clientList() { return m_clients; }

    QcServerConfig m_config;
    std::shared_ptr<const QcContentRegistry> m_content;
    QcServerWorld m_world;
    std::vector<std::unique_ptr<QcServerClient>> m_clients;
    QcEntities m_entities;
    QcSpawnPoint m_spawnPoint;
    QcContactDamage m_contactDamage;
    QcWorldInfo m_info;
    QcAccessList m_access;
    QcIdentityStore m_identities;
    QcServerClock m_clock;
    QcPlantManager m_plants;
    QcPlantGrowth m_growth;
    QcMachineRules m_machineRules;
    QcLiquidFlow m_liquids;
    QcCircuitState m_circuits;
    QcHopperQueue m_hoppers;
    QcMapStore m_maps;
    QRandomGenerator m_random;
    QRandomGenerator m_soundRandom;
    bool m_playerListChanged = false;
    bool m_latencyChanged = false;
    // Players' time when the list last went out.
    qint64 m_playerListNs = 0;
    // Last: it reads the content and the clients through this state.
    QcServerAdvancements m_advancements{*this};
};
