// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "server/advancements/QcServerAdvancements.h"
#include "server/commands/QcPermission.h"
#include "server/core/QcContactDamage.h"
#include "server/core/QcServerClient.h"
#include "server/core/QcServerConfig.h"
#include "server/core/QcServerWorld.h"
#include "server/core/QcSpawnPoint.h"
#include "server/dimensions/QcDimensions.h"
#include "server/login/QcAccessList.h"
#include "server/login/QcIdentityStore.h"
#include "server/machines/QcMachineRules.h"
#include "server/maps/QcMapStore.h"
#include "server/storage/QcWorldStorage.h"
#include "server/tick/QcServerClock.h"

#include <QRandomGenerator>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

/*! \brief The world, the clients and the entities of the authoritative server, shared by its parts
    (decision E-367): each part is reached through its own accessor and keeps its data behind its
    own interface; what changes at the level of the whole server (the clock, the player list)
    changes through a named function.

    What belongs to a dimension (the world, the entities, the plants, the liquids, the circuits,
    the hoppers and the clients in it) is answered for the current dimension (decision E-988),
    the overworld unless a Scope says otherwise: the tick runs its systems once per open
    dimension, a client's messages and sync run in his dimension, and the parts need not know
    of dimensions.

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

    /*! \brief While it lives, \a dimension is the current dimension; then the one before it. */
    class [[nodiscard]] Scope
    {
    public:
        Scope(QcServerState &state, QcServerDimension &dimension)
            : m_state(state)
            , m_previous(std::exchange(state.m_current, &dimension))
        {
        }
        ~Scope() { m_state.m_current = m_previous; }
        Q_DISABLE_COPY_MOVE(Scope)

    private:
        QcServerState &m_state;
        QcServerDimension *m_previous;
    };
    /*! Returns the dimension the parts act in now. */
    QcServerDimension &current() { return *m_current; }
    /*! Returns the dimension the parts act in now, read-only. */
    const QcServerDimension &current() const { return *m_current; }

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
        hoppers().touched.insert(pos);
        circuits().noteContents(world(), pos);
    }
    /*! Returns the state that belongs to single blocks (decision E-357). */
    QcBlockEntities &blockEntities() { return world().blockEntities(); }
    /*! Returns the state that belongs to single blocks, read-only. */
    const QcBlockEntities &blockEntities() const { return world().blockEntities(); }
    /*! Hands the limits of the config to the parts that keep their own copy (decision E-349), in
        every open dimension. */
    void applyLimits();

    /*! Returns the server's settings; a single-player world's limits replace those of the config
        (world.json). */
    QcServerConfig &config() { return m_config; }
    /*! Returns the server's settings, read-only. */
    const QcServerConfig &config() const { return m_config; }
    /*! Returns the content: blocks, items, creatures and the rest. */
    const QcContentRegistry &content() const { return *contentPointer(); }
    /*! Returns the shared pointer to the content, for parts that keep it. */
    const std::shared_ptr<const QcContentRegistry> &contentPointer() const { return m_content; }
    /*! Returns the world of the current dimension: chunks, blocks and what belongs to them. */
    QcServerWorld &world() { return current().world(); }
    /*! Returns the world of the current dimension, read-only. */
    const QcServerWorld &world() const { return current().world(); }
    /*! Returns the open dimensions (decision E-978). */
    QcDimensions &dimensions() { return m_dimensions; }
    /*! Returns the open dimensions, read-only. */
    const QcDimensions &dimensions() const { return m_dimensions; }
    /*! Opens the dimension \a name (decision E-978) unless it is open: generated from the
        world's seed as the generator version world.json names for it, or, when it names none,
        the version it gets for good from now on (QcDimensions::versionIn), kept in info(), and
        with the world limits of the config. Returns an error for a dimension the content does
        not have, a generator type or options it cannot make, a version this game does not
        generate, too many open dimensions, or a world that cannot record another version
        (QcWorldInfo::maxDimensionVersions), or a storage that cannot open (setDimensionStorage);
        nothing is recorded then and the dimension is not open. */
    [[nodiscard]] QcResult<QcServerDimension *> openDimension(const QString &name);
    /*! Connects a dimension to the parts of the server. */
    using DimensionWiring = std::function<void(QcServerDimension &)>;
    /*! Calls \a wiring with every open dimension and with each that opens from now on, before it
        plays; the parts set their handlers there. */
    void addDimensionWiring(DimensionWiring wiring);
    /*! Opens what is stored of a dimension that opens, in its Scope. */
    using DimensionStorage = std::function<QcStatus(QcServerDimension &)>;
    /*! Sets \a storage, called for every dimension that opens but the overworld. */
    void setDimensionStorage(DimensionStorage storage) { m_dimensionStorage = std::move(storage); }
    /*! Ends the screens of a player, wherever he is. */
    using ScreenCloser = std::function<void(QcServerClient &)>;
    /*! Sets \a closer, called by closeScreens(). */
    void setScreenCloser(ScreenCloser closer) { m_screenCloser = std::move(closer); }
    /*! Ends every screen \a client has open, in the current dimension. */
    void closeScreens(QcServerClient &client) const;
    /*! Returns the clients in the current dimension; the parts may change a client, only
        QcServer adds and removes them. Who has not joined yet counts to the overworld. */
    const QcServerDimension::Clients &clients() const { return current().clients(); }
    /*! Returns the clients of every dimension. */
    const std::vector<std::unique_ptr<QcServerClient>> &allClients() const { return m_clients; }
    /*! Moves \a client from his dimension to \a dimension: its parts see him from now on, and a
        client who joined is told the new dimension (QcMsg::DimensionChange), so it forgets the
        chunks, creatures and weather it was sent. QcPlayerTravel moves a player who plays. */
    void moveClient(QcServerClient &client, QcServerDimension &dimension);
    /*! Returns the open dimension \a client is in. */
    QcServerDimension &dimensionOf(const QcServerClient &client);
    /*! Returns every entity of the current dimension (decision E-364); players are the clients. */
    QcEntities &entities() { return current().entities(); }
    /*! Returns every entity of the current dimension, read-only. */
    const QcEntities &entities() const { return current().entities(); }
    /*! Returns where players appear: the world spawn in spawnDimension(). */
    QcSpawnPoint &spawnPoint() { return m_spawnPoint; }
    /*! Returns the dimension of the world spawn: the world's start dimension once
        openSpawnDimension() opened it (decision E-1110), the overworld before and when it cannot
        open. */
    QcServerDimension &spawnDimension() { return *m_spawnDimension; }
    /*! Opens the start dimension of the world (QcWorldInfo::startDimension) and puts the world
        spawn there. A dimension that cannot open, or where no world may start, leaves the spawn
        in the overworld; returns the warning then, else an empty string. */
    QString openSpawnDimension();
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
    QcPlantManager &plants() { return current().plants(); }
    /*! Returns the growing plants, read-only. */
    const QcPlantManager &plants() const { return current().plants(); }
    /*! Returns the random growth of plants, crops and soil. */
    QcPlantGrowth &growth() { return current().growth(); }
    /*! Returns the rules of machines. */
    const QcMachineRules &machineRules() const { return m_machineRules; }
    /*! Returns the flow of liquids. */
    QcLiquidFlow &liquids() { return current().liquids(); }
    /*! Returns the flow of liquids, read-only. */
    const QcLiquidFlow &liquids() const { return current().liquids(); }
    /*! Returns the Signal Dust circuits (decision E-335). */
    QcCircuitState &circuits() { return current().circuits(); }
    /*! Returns the Signal Dust circuits, read-only. */
    const QcCircuitState &circuits() const { return current().circuits(); }
    /*! Returns the hoppers to look at (decision E-344). */
    QcHopperQueue &hoppers() { return current().hoppers(); }
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
    // Adds a client that connected; he counts to the overworld until he joins.
    void addClient(std::unique_ptr<QcServerClient> client);
    // Removes the clients that closed; returns how many.
    qsizetype removeClosedClients();

    QcServerConfig m_config;
    std::shared_ptr<const QcContentRegistry> m_content;
    QcDimensions m_dimensions;
    // What the parts act in: never null, an open dimension (they stay open).
    QcServerDimension *m_current = &m_dimensions.overworld();
    std::vector<DimensionWiring> m_dimensionWirings;
    DimensionStorage m_dimensionStorage;
    ScreenCloser m_screenCloser;
    std::vector<std::unique_ptr<QcServerClient>> m_clients;
    QcSpawnPoint m_spawnPoint;
    // Where m_spawnPoint is: never null, an open dimension.
    QcServerDimension *m_spawnDimension = &m_dimensions.overworld();
    QcContactDamage m_contactDamage;
    QcWorldInfo m_info;
    QcAccessList m_access;
    QcIdentityStore m_identities;
    QcServerClock m_clock;
    QcMachineRules m_machineRules;
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
