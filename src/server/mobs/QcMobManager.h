// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcChunkPos.h"
#include "common/core/QcCreatureKindId.h"
#include "common/physics/QcEntityMotion.h"
#include "common/physics/QcPhysics.h"
#include "server/entities/QcCreature.h"
#include "server/entities/QcEntityComponent.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/mobs/QcCreatureBodies.h"
#include "server/mobs/QcMobCrowd.h"
#include "server/mobs/QcMobExchange.h"
#include "server/projectiles/QcProjectileManager.h"
#include "server/villages/QcVillager.h"

#include <QList>
#include <QRandomGenerator>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

class QcChunk;
class QcMobBehaviour;
class QcNaturalSpawner;
class QcPathPlanner;
class QcServerWorld;
class QcVillagePopulation;
class QcVillagerBehaviour;

/*! \brief The creatures on the server (decision E-422): their store, the order of their tick
    (behaviour in QcMobBehaviour: wander, chase along paths, attack, flee when hurt for passive
    mobs; despawning; natural spawning in QcNaturalSpawner; village life in QcVillagePopulation),
    damage, and saving and loading the persistent ones.

    Peaceful animals (not hostile, ambient or flying) are persistent (decision E-086): they never
    despawn, rest while their chunk is not loaded and are saved with the world. Other creatures
    are not saved; they appear again around the players. Every creature that leaves the store
    raises one notification with the reason (Departure) for its listeners. */
class QcMobManager
{
public:
    /*! Creatures that are not kept vanish at once beyond this distance from every player
        (decision E-836). */
    static constexpr double despawnDistance = 128.0;
    /*! Flying creatures keep between these heights above the ground below them. \qcshared */
    static constexpr double flyMinHeight = 1.2;
    static constexpr double flyMaxHeight = 3.0;
    /*! The horizontal speed of a blow's push at strength 1 (m/s). */
    static constexpr double knockbackSpeed = 6.0;
    /*! The upward speed of a blow's push at strength 1; a weaker push lifts less, a stronger one
        only pushes further (decision E-427). */
    static constexpr double knockbackLift = 5.0;

    /*! Returns the velocity of a push along \a away (horizontal, normalized) with \a strength
        times a blow's push, lifting \a lift times a blow's lift (a golem's blow throws upwards,
        decision E-471). */
    static QcVec3 knockbackVelocity(const QcVec3 &away, double strength, double lift = 1.0);

    /*! The data exchanged with the server and the brains (QcMobExchange): a saved mob. */
    using Stored = QcMobExchange::Stored;
    /*! What the mob logic knows about a player. */
    using PlayerView = QcMobExchange::PlayerView;
    /*! A block a creature takes or puts down. */
    using BlockAction = QcMobExchange::BlockAction;
    /*! A blow of a creature at a player. */
    using PlayerHit = QcMobExchange::PlayerHit;
    /*! A blow of one creature at another. */
    using CreatureHit = QcMobExchange::CreatureHit;

    /*! \brief Why a creature left the store: it died, was removed without a death (caught,
        commands), despawned far from the players or in daylight, fell into the void, or its
        chunk unloaded (it rests in the chunk's entity record and comes back with it). */
    enum class Departure : quint8 {
        /*! It died. */
        Died,
        /*! It was removed without a death (caught, commands). */
        Removed,
        /*! It despawned far from the players or in daylight. */
        Despawned,
        /*! It fell into the void. */
        Void,
        /*! Its chunk unloaded. */
        Unloaded
    };
    /*! Called with the creature just before it leaves the store. Every creature that leaves in
        the same step is still in the store while the listeners run, so they may read it (find,
        mobs()); they must not add or remove creatures. */
    using DepartureListener = std::function<void(const QcCreature &mob, Departure why)>;
    /*! \brief Names a subscription to departures (onDeparture), for removeDepartureListener. */
    enum class DepartureToken : quint64 {};

    QcMobManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed);
    ~QcMobManager();
    QcMobManager(const QcMobManager &) = delete;
    QcMobManager &operator=(const QcMobManager &) = delete;
    /*! Returns the natural spawning around players and with new chunks: caps, spawn chunks
        (decision E-086). */
    QcNaturalSpawner &spawner() { return *m_spawner; }
    /*! Returns the natural spawning, read-only. */
    const QcNaturalSpawner &spawner() const { return *m_spawner; }
    /*! Returns the villagers of the villages: limits, claims, children and golems (decisions
        E-411, E-415). */
    QcVillagePopulation &population() { return *m_population; }
    /*! Returns the villagers of the villages, read-only. */
    const QcVillagePopulation &population() const { return *m_population; }
    /*! Subscribes \a listener to every departure from the store until the token it returns is
        removed or the manager goes. */
    DepartureToken onDeparture(DepartureListener listener);
    /*! Ends the subscription \a token; takes effect from the next departure on (also inside a
        listener). */
    void removeDepartureListener(DepartureToken token);

    /*! Sets the budgets of the path searches: \a nodesPerTick, \a nodesPerSearch, \a pathLength
        (QcServerLimits pathNodesPerTick, pathNodesPerSearch, pathLength). */
    void setPathLimits(qint64 nodesPerTick, qint64 nodesPerSearch, qint64 pathLength);
    /*! Returns the path searches of the creatures (budget use, tests and debug). */
    const QcPathPlanner &paths() const;
    /*! Sets the thresholds of the AI tiers in crowds to \a throttled and \a idle (QcServerLimits
        crowdThrottled, crowdIdle; decision E-401). */
    void setCrowdLimits(qint64 throttled, qint64 idle);
    /*! Returns what the creatures did in the last tick: crowds, tiers, paths (tests, measurements).
    */
    const QcMobBehaviour &behaviour() const { return *m_behaviour; }
    /*! Returns the creatures that stood in a cell with more than \a limit creatures at the start
        of the last tick (game rule maxEntityCramming; 0: none). */
    QList<QcMobCrowd::Crammed> crammed(int limit) const;

    /*! Returns the villagers' day, doors, roads and trades (decision E-411). */
    QcVillagerBehaviour &villagers();
    /*! Returns the villagers' behaviour, read-only. */
    const QcVillagerBehaviour &villagers() const;

    /*! Advances all mobs in \a world among \a players by one tick of \a dt seconds at \a worldTime,
        new creatures with ids from \a nextEntityId, and returns the hits they dealt to players. */
    QList<PlayerHit> tick(QcServerWorld &world, const QList<PlayerView> &players, double worldTime, double dt,
                          const std::function<quint32()> &nextEntityId);
    /*! Returns the shots of the creatures since the last call (QcMobBrain::Context::launches,
        decision E-425); the server adds them as projectiles after the tick. */
    QList<QcProjectileManager::Launch> takeLaunches();
    /*! Returns the blows of creatures at creatures since the last call
        (QcMobBrain::Context::creatureHits). */
    QList<CreatureHit> takeCreatureHits();
    /*! Returns the blocks the creatures took or put down since the last call (decision E-460). */
    QList<BlockAction> takeBlockActions();
    /*! Sets \a random as the sequence the yaws of new creatures come from (the monster spawning's
        own, decision E-836); null returns to the creatures' sequence. It must outlive its use. */
    void setYawRandom(QRandomGenerator *random) { m_yawRandom = random ? random : &m_random; }
    /*! Sets the game rule mobGriefing to \a on (decision E-460): without it creatures take and
        place no blocks. */
    void setGriefing(bool on);
    /*! Sets the strengths 0..1 of \a rain and \a thunder (QcWeatherState), which darken the sky
        for the monsters' light test (QcSpawnLight::skyDarkening). */
    void setWeather(float rain, float thunder)
    {
        m_rain = rain;
        m_thunder = thunder;
    }
    /*! Teleports the creature \a mobId to a safe place near it in \a world (QcMobTeleport) when
        its kind teleports and it may now; returns true when it did. */
    bool teleport(quint32 mobId, const QcBlockAccess &world);
    /*! Keeps \a mob with the world (persistent, with a persistent id) from now on, or no longer
        (\a kept); never a guardian (QcCreature::guards, decision E-591); a named creature always
        stays kept (decision E-866). A parent of a family takes its children along (decision
        E-839). */
    void keep(QcCreature &mob, bool kept);
    /*! Lets \a mob carry the block state \a block (qcAirBlockId: none) with its item in the
        hand. */
    void carryBlock(QcCreature &mob, QcBlockId block) const;
    /*! Applies \a blow to its creature: damage from \c {blow.from}, pushing it away with \c
        {knockback} times a blow's push (0: not at all) and \c {lift} times its lift, less what the
        kind resists (a golem stands firm, decision E-471); returns the mob's drops when it died. A
        hit without damage (a snowball) pushes and frightens but gives no invulnerability (decision
        E-427). During the creature's invulnerability only a stronger hit takes, with the difference
        and without the push (QcHurtWindow, decision E-429). \c {blow.attacker} is the player who
        dealt it (0: none, as for lightning or a golem's blow): a neutral creature turns on that
        player (QcCreature::struckBy, decision E-460). */
    std::optional<QList<QcItemDrop>> damage(const QcServerCombat::Blow &blow);
    /*! Deals \a amount damage without a source (lava) to the mob \a mobId: no knockback, no
        fleeing. Returns true when the mob died and was removed. */
    bool hurt(quint32 mobId, double amount);
    /*! Deals \a amount damage like hurt() to the mob \a mobId; returns the mob's drops when it
        died (cramming, decision E-401). */
    std::optional<QList<QcItemDrop>> wound(quint32 mobId, double amount);
    /*! Deals \a amount damage like wound() to the mob at \a index in mobs() and returns its drops
        when it died; a death removes it from the list. */
    std::optional<QList<QcItemDrop>> woundAt(qsizetype index, double amount);
    /*! Removes the mob \a mobId without a death (caught); returns false when there is none. */
    bool remove(quint32 mobId);
    /*! Takes the mobs \a leaves picks out of the store because their chunk unloads
        (Departure::Unloaded). */
    void unload(const std::function<bool(const QcCreature &mob)> &leaves);
    /*! Spawns a mob of the kind \a type at \a position with the id \a id directly (commands,
        tests), in a cave when \a underground, kept with the world when \a persistent; returns its
        id. */
    quint32 spawn(QcCreatureKindId type, const QcVec3 &position, quint32 id, bool underground = false, bool persistent = false);
    /*! Clears the store without departures (tests that want an empty world). */
    void removeAll()
    {
        m_mobs.clear();
        ++m_generation;
    }
    /*! Sets the game rule mobSpawning to \a on: without it no creature spawns by itself, neither
        around players nor with new chunks (decision E-221); /summon and world setups still add
        them. */
    void setNaturalSpawning(bool on) { m_naturalSpawning = on; }
    /*! Returns whether creatures spawn by themselves (setNaturalSpawning()). */
    bool naturalSpawning() const { return m_naturalSpawning; }
    /*! Sets the game rule mobInvulnerabilityTicks to \a ticks (decision E-429): the game ticks a
        creature stays invulnerable after a hurt (0: every hit counts). A lower value shortens the
        open windows at once. */
    void setHurtTicks(int ticks);
    /*! Returns the game ticks a creature stays invulnerable after a hurt (setHurtTicks()). */
    int hurtTicks() const { return m_hurtTicks; }

    /*! Gives the chunk at \a pos of \a world seen for the first time, with ids from \a
        nextEntityId, the creatures of the village pieces in it (an animal pen's pair, decision
        E-404), a villager for each free bed in it (decision E-411) and its group of animals
        (QcNaturalSpawner::chunkAnimalChance, decided by the seed), and remembers it; later calls
        for the same chunk do nothing. The chunk must be loaded. */
    void populateChunk(QcServerWorld &world, QcChunkPos pos, const std::function<quint32()> &nextEntityId);
    /*! Returns whether the chunk at \a pos got its creatures (populateChunk()). */
    bool isPopulated(QcChunkPos pos) const { return m_populated.contains(pos); }
    /*! Returns every chunk that got its creatures. */
    const QSet<QcChunkPos> &populatedChunks() const { return m_populated; }
    /*! The chunk at \a pos left the world; whether it got its animals goes with it (decision
        E-374). */
    void forgetPopulated(QcChunkPos pos) { m_populated.remove(pos); }
    /*! Marks the chunk at \a pos as one that got its creatures (a chunk that came back). */
    void notePopulated(QcChunkPos pos) { m_populated.insert(pos); }
    /*! Returns the persistent mobs to save. */
    QList<Stored> persistentMobs() const;
    /*! Returns \a mob as saved with the world. */
    static Stored storedOf(const QcCreature &mob);
    /*! Adds the saved \a mobs as dormant with ids from \a nextEntityId and takes \a populated as
        the chunks that got their creatures. */
    void restore(const QList<Stored> &mobs, const QSet<QcChunkPos> &populated, const std::function<quint32()> &nextEntityId);
    /*! Links the children of kept families that wait for their parent (QcCreature::parentUid) to
        the parent when it is in the world (decision E-839), no more than its family's \c
        {maxChildren} (the others are released); restore does so after every load. */
    void relinkFamilies();
    /*! Releases \a child from its family: no longer kept, no parent; it goes on as an ordinary
        monster. */
    void release(QcCreature &child);
    /*! Returns the persistent id the next saved creature gets (QcCreature::uid); world.json
        keeps it. Setting never lowers it below an id already given. */
    quint64 nextUid() const { return m_nextUid; }
    /*! Sets nextUid() to \a next, unless that is lower. */
    void setNextUid(quint64 next) { m_nextUid = std::max(m_nextUid, next); }
    /*! Returns the number of mobs that are not dormant. */
    qsizetype awakeCount() const;
    /*! Returns the number of mobs that are not dormant and came by /summon. */
    qsizetype awakeSummonedCount() const;
    /*! Returns every mob, dormant ones too. */
    const QList<QcCreature> &mobs() const { return m_mobs; }
    /*! Returns the push of the creatures on a player with their feet at \a feet in server tick
        \a tick (decision E-882), a horizontal velocity for their next step
        (QcPlayerInput::push). */
    [[nodiscard]] QcVec3 pushOnPlayer(const QcVec3 &feet, quint64 tick)
    {
        return m_bodies.pushOnPlayer(feet, m_mobs, m_content->mobs(), tick, m_generation);
    }
    /*! Returns every mob for changes in place: for the breeding timers (QcMobBreeding) and the
        parts that set up a mob they just spawned; never add or remove mobs through it. */
    QList<QcCreature> &editableMobs() { return m_mobs; }
    /*! Returns the mob with the entity id \a id, or null. */
    const QcCreature *find(quint32 id) const;
    /*! Returns the mob with the entity id \a id for changes, or null. */
    QcCreature *find(quint32 id);

private:
    // A wild creature of a pack (QcMobDefinition::packRange, decision E-480) struck by `attacker`:
    // the wild ones of its kind within the range turn on that player too.
    void angerPack(const QcCreature &mob, quint32 attacker);
    // Tells the listeners that `mob` leaves the store.
    void depart(const QcCreature &mob, Departure why);
    // Erases the mob at `it` after its departure.
    void erase(QList<QcCreature>::iterator it, Departure why);
    // The id a loaded creature keeps: its saved one, unless `taken` has it. A copied chunk (by hand
    // or by a tool) brings creatures whose ids are already in the world; the copy gets the next free
    // one, so every id names one creature.
    [[nodiscard]] quint64 uniqueUid(quint64 saved, const QSet<quint64> &taken);
    // Takes the mobs `leaves` gives a reason for out of the store: first all of them are found, then
    // the listeners hear of each in store order, then they are erased.
    void departWhere(const std::function<std::optional<Departure>(const QcCreature &mob)> &leaves);

    std::shared_ptr<const QcContentRegistry> m_content;
    quint64 m_seed = 0;
    QRandomGenerator m_random;
    // The chance of idle creatures to vanish (QcMobUpkeep::idleChance), from the seed.
    QRandomGenerator m_despawnRandom;
    // The sequence a new creature's yaw comes from: m_random unless setYawRandom says otherwise.
    QRandomGenerator *m_yawRandom = nullptr;
    // Uses m_random, so it comes after it.
    std::unique_ptr<QcMobBehaviour> m_behaviour;
    QList<QcCreature> m_mobs;
    QcCreatureBodies m_bodies;
    // Counts every creature added to or removed from m_mobs, so that the creatures' push on players
    // (m_bodies) knows when its grid is out of date between two ticks.
    quint64 m_generation = 0;
    QSet<QcChunkPos> m_populated;
    quint64 m_nextUid = 1;
    bool m_naturalSpawning = true;
    float m_rain = 0.0f;
    float m_thunder = 0.0f;
    int m_hurtTicks = QcHurtWindow::defaultTicks;
    QList<std::pair<DepartureToken, DepartureListener>> m_departureListeners;
    quint64 m_nextDepartureToken = 1;
    // Use m_random and subscribe to departures, so they come last.
    std::unique_ptr<QcNaturalSpawner> m_spawner;
    std::unique_ptr<QcVillagePopulation> m_population;
};
