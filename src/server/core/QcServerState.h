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

/*! \brief The world, the clients and the entities of the authoritative server, shared by its parts
    (decision E-367): each part is reached through its own accessor and keeps its data behind its
    own interface; what changes at the level of the whole server (the clock, the player list)
    changes through a named function.

    Lives in the server's thread like QcServer itself. */
class QcServerState
{
public:
    QcServerState(QcServerConfig serverConfig, std::shared_ptr<const QcContentRegistry> registry,
                  std::unique_ptr<QcWorldGenerator> generator);
    /*! The parts keep references into it (QcEntities to the clients, QcSpawnPoint to the world). */
    QcServerState(const QcServerState &) = delete;
    QcServerState &operator=(const QcServerState &) = delete;
    ~QcServerState() = default;

    /*! Returns whether the chunk of \a pos is loaded. */
    bool isLoadedAt(QcBlockPos pos) const;
    /*! Returns the definition of the item \a client holds in the selected slot; nullptr for an
        empty hand or an unknown item. */
    const QcItemDefinition *heldItem(const QcServerClient &client) const;
    /*! Returns the right of \a client: the local player (host of a single player world) and
        listed operators are operators, the others players (decision E-243). */
    QcPermission permissionOf(const QcServerClient &client) const;
    /*! The contents of the container or machine at \a pos changed (a screen, a hopper, a
        furnace): comparators reading it and hoppers next to it look again (decisions E-343,
        E-344). */
    void noteContents(QcBlockPos pos)
    {
        m_hoppers.touched.insert(pos);
        m_circuits.noteContents(m_world, pos);
    }
    /*! Returns the state that belongs to single blocks (decision E-357). */
    QcBlockEntities &blockEntities() { return m_world.blockEntities(); }
    /*! Returns the state that belongs to single blocks, read-only. */
    const QcBlockEntities &blockEntities() const { return m_world.blockEntities(); }
    /*! Hands the limits of the config to the parts that keep their own copy (decision E-349). */
    void applyLimits();

    /*! Returns the server's settings; a single-player world's limits replace those of the config
        (world.json). */
    QcServerConfig &config() { return m_config; }
    /*! Returns the server's settings, read-only. */
    const QcServerConfig &config() const { return m_config; }
    /*! Returns the content: blocks, items, creatures and the rest. */
    const QcContentRegistry &content() const { return *m_content; }
    /*! Returns the shared pointer to the content, for parts that keep it. */
    const std::shared_ptr<const QcContentRegistry> &contentPointer() const { return m_content; }
    /*! Returns the world: chunks, blocks and what belongs to them. */
    QcServerWorld &world() { return m_world; }
    /*! Returns the world, read-only. */
    const QcServerWorld &world() const { return m_world; }
    /*! Returns the connected clients; the parts may change a client, only QcServer adds and
        removes them. */
    const std::vector<std::unique_ptr<QcServerClient>> &clients() const { return m_clients; }
    /*! Returns every entity of the world (decision E-364); players are the clients. */
    QcEntities &entities() { return m_entities; }
    /*! Returns every entity of the world, read-only. */
    const QcEntities &entities() const { return m_entities; }
    /*! Returns where players appear. */
    QcSpawnPoint &spawnPoint() { return m_spawnPoint; }
    /*! Returns the damage of the blocks bodies touch (lava, magma). */
    const QcContactDamage &contactDamage() const { return m_contactDamage; }
    /*! Returns world.json: name, seed, time of day, game rules. */
    QcWorldInfo &info() { return m_info; }
    /*! Returns world.json, read-only. */
    const QcWorldInfo &info() const { return m_info; }
    /*! Returns who may join and who is an operator. */
    QcAccessList &access() { return m_access; }
    /*! Returns who may join and who is an operator, read-only. */
    const QcAccessList &access() const { return m_access; }
    /*! Returns the identities of remote players per name (decision E-252). */
    QcIdentityStore &identities() { return m_identities; }
    /*! Returns the clock, read by every part; only QcServer advances it, once per tick. */
    const QcServerClock &clock() const { return m_clock; }
    /*! Returns the growing plants. */
    QcPlantManager &plants() { return m_plants; }
    /*! Returns the growing plants, read-only. */
    const QcPlantManager &plants() const { return m_plants; }
    /*! Returns the random growth of plants, crops and soil. */
    QcPlantGrowth &growth() { return m_growth; }
    /*! Returns the rules of machines. */
    const QcMachineRules &machineRules() const { return m_machineRules; }
    /*! Returns the flow of liquids. */
    QcLiquidFlow &liquids() { return m_liquids; }
    /*! Returns the flow of liquids, read-only. */
    const QcLiquidFlow &liquids() const { return m_liquids; }
    /*! Returns the Signal Dust circuits (decision E-335). */
    QcCircuitState &circuits() { return m_circuits; }
    /*! Returns the Signal Dust circuits, read-only. */
    const QcCircuitState &circuits() const { return m_circuits; }
    /*! Returns the hoppers to look at (decision E-344). */
    QcHopperQueue &hoppers() { return m_hoppers; }
    /*! Returns the maps of the world by map id (decision E-799). */
    QcMapStore &maps() { return m_maps; }
    /*! Returns the maps of the world by map id, read-only. */
    const QcMapStore &maps() const { return m_maps; }
    /*! Returns the adventure guide (decision E-923): the gameplay reports events to it. */
    QcServerAdvancements &advancements() { return m_advancements; }
    /*! Returns the random generator of the gameplay (drops, spawns). */
    QRandomGenerator &random() { return m_random; }
    /*! Returns the random generator of sounds, separate from random() so that sounds never
        change drops or spawns. */
    QRandomGenerator &soundRandom() { return m_soundRandom; }

    /*! Someone joined or left, or operator rights changed: the clients get the player list
        again. */
    void notePlayerListChanged() { m_playerListChanged = true; }
    /*! Round trips go out in the list at most once per this long of the players' time. */
    static constexpr qint64 latencyListIntervalNs = 2'000'000'000;
    /*! \a client reported its round trip \a reportedUs in microseconds (decision E-886). A
        change that shows in the list (QcListedLatency::changed) is listed and the list goes out
        again, at most once per latencyListIntervalNs of the players' time; smaller changes are
        ignored. */
    void reportLatency(QcServerClient &client, qint32 reportedUs);
    /*! Returns whether the list changed since the last call, or round trips changed and their
        interval passed. */
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
