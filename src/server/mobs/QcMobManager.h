#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcChunkPos.h"
#include "common/physics/QcEntityMotion.h"
#include "common/physics/QcPhysics.h"
#include "server/entities/QcCreature.h"
#include "server/entities/QcEntityComponent.h"
#include "server/gameplay/QcServerCombat.h"
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

// The creatures on the server (decision E-422): their store, the order of their tick (behaviour
// in QcMobBehaviour: wander, chase along paths, attack, flee when hurt for passive mobs; despawning;
// natural spawning in QcNaturalSpawner; village life in QcVillagePopulation), damage, and saving and
// loading the persistent ones. Peaceful animals (not hostile, ambient or flying) are persistent
// (decision E-086): they never despawn, rest while their chunk is not loaded and are saved with the
// world. Other creatures are not saved; they appear again around the players. Every creature that
// leaves the store raises one notification with the reason (Departure) for its listeners.
class QcMobManager
{
public:
    // Creatures that are not kept vanish at once beyond this distance from every player (decision E-836).
    static constexpr double despawnDistance = 128.0;
    // Flying creatures keep between these heights above the ground below them.
    static constexpr double flyMinHeight = 1.2;
    static constexpr double flyMaxHeight = 3.0;
    static constexpr double knockbackSpeed = 6.0;
    // The upward speed of a blow's push at strength 1; a weaker push lifts less, a stronger one
    // only pushes further (decision E-427).
    static constexpr double knockbackLift = 5.0;

    // The velocity of a push along `away` (horizontal, normalized) with `strength` times a blow's
    // push, lifting `lift` times a blow's lift (a golem's blow throws upwards, decision E-471).
    static QcVec3 knockbackVelocity(const QcVec3 &away, double strength, double lift = 1.0);

    // The data exchanged with the server and the brains (QcMobExchange).
    using Stored = QcMobExchange::Stored;
    using PlayerView = QcMobExchange::PlayerView;
    using BlockAction = QcMobExchange::BlockAction;
    using PlayerHit = QcMobExchange::PlayerHit;
    using CreatureHit = QcMobExchange::CreatureHit;

    // Why a creature left the store: it died, was removed without a death (caught, commands),
    // despawned far from the players or in daylight, fell into the void, or its chunk unloaded (it
    // rests in the chunk's entity record and comes back with it).
    enum class Departure : quint8 { Died, Removed, Despawned, Void, Unloaded };
    // Called with the creature just before it leaves the store. Every creature that leaves in the
    // same step is still in the store while the listeners run, so they may read it (find, mobs());
    // they must not add or remove creatures.
    using DepartureListener = std::function<void(const QcCreature &mob, Departure why)>;
    // Names a subscription to departures (onDeparture), for removeDepartureListener.
    enum class DepartureToken : quint64 {};

    QcMobManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed);
    ~QcMobManager();
    QcMobManager(const QcMobManager &) = delete;
    QcMobManager &operator=(const QcMobManager &) = delete;
    // Natural spawning around players and with new chunks: caps, spawn chunks (decision E-086).
    QcNaturalSpawner &spawner() { return *m_spawner; }
    const QcNaturalSpawner &spawner() const { return *m_spawner; }
    // The villagers of the villages: limits, claims, children and golems (decisions E-411, E-415).
    QcVillagePopulation &population() { return *m_population; }
    const QcVillagePopulation &population() const { return *m_population; }
    // Subscribes to every departure from the store until the token is removed or the manager goes.
    DepartureToken onDeparture(DepartureListener listener);
    // Ends a subscription; takes effect from the next departure on (also inside a listener).
    void removeDepartureListener(DepartureToken token);

    // Budgets of the path searches (QcServerLimits pathNodesPerTick, pathNodesPerSearch, pathLength).
    void setPathLimits(qint64 nodesPerTick, qint64 nodesPerSearch, qint64 pathLength);
    // The path searches of the creatures (budget use, tests and debug).
    const QcPathPlanner &paths() const;
    // Thresholds of the AI tiers in crowds (QcServerLimits crowdThrottled, crowdIdle; decision E-401).
    void setCrowdLimits(qint64 throttled, qint64 idle);
    // What the creatures did in the last tick: crowds, tiers, paths (tests, measurements).
    const QcMobBehaviour &behaviour() const { return *m_behaviour; }
    // The creatures that stood in a cell with more than `limit` creatures at the start of the last
    // tick (game rule maxEntityCramming; 0: none).
    QList<QcMobCrowd::Crammed> crammed(int limit) const;

    // The villagers' day, doors, roads and trades (decision E-411).
    QcVillagerBehaviour &villagers();
    const QcVillagerBehaviour &villagers() const;

    // Advances all mobs by one tick and returns the hits they dealt to players.
    QList<PlayerHit> tick(QcServerWorld &world, const QList<PlayerView> &players, double worldTime, double dt,
                          const std::function<quint32()> &nextEntityId);
    // The shots of the creatures since the last call (QcMobBrain::Context::launches, decision E-425);
    // the server adds them as projectiles after the tick.
    QList<QcProjectileManager::Launch> takeLaunches();
    // The blows of creatures at creatures since the last call (QcMobBrain::Context::creatureHits).
    QList<CreatureHit> takeCreatureHits();
    // The blocks the creatures took or put down since the last call (decision E-460).
    QList<BlockAction> takeBlockActions();
    // The sequence the yaws of new creatures come from while it is set (the monster spawning's own,
    // decision E-836); null returns to the creatures' sequence. It must outlive its use.
    void setYawRandom(QRandomGenerator *random) { m_yawRandom = random ? random : &m_random; }
    // Game rule mobGriefing (decision E-460): without it creatures take and place no blocks.
    void setGriefing(bool on);
    // The strengths 0..1 of rain and thunder (QcWeatherState), which darken the sky for the monsters'
    // light test (QcSpawnLight::skyDarkening).
    void setWeather(float rain, float thunder)
    {
        m_rain = rain;
        m_thunder = thunder;
    }
    // Teleports the creature to a safe place near it (QcMobTeleport) when its kind teleports and it
    // may now; true when it did.
    bool teleport(quint32 mobId, const QcBlockAccess &world);
    // Keeps a creature with the world (persistent, with a persistent id) from now on, or no longer;
    // never a guardian (QcCreature::guards, decision E-591).
    void keep(QcCreature &mob, bool kept);
    // Lets the creature carry the block state (qcAirBlockId: none) with its item in the hand.
    void carryBlock(QcCreature &mob, QcBlockId block) const;
    // Applies `blow` to its creature: damage from `blow.from`, pushing it away with `knockback` times
    // a blow's push (0: not at all) and `lift` times its lift, less what the kind resists (a golem
    // stands firm, decision E-471); returns the mob's drops when it died. A hit without damage (a
    // snowball) pushes and frightens but gives no invulnerability (decision E-427). During the
    // creature's invulnerability only a stronger hit takes, with the difference and without the
    // push (QcHurtWindow, decision E-429). `blow.attacker` is the player who dealt it (0: none, as
    // for lightning or a golem's blow): a neutral creature turns on that player
    // (QcCreature::struckBy, decision E-460).
    std::optional<QList<QcItemDrop>> damage(const QcServerCombat::Blow &blow);
    // Damage without a source (lava): no knockback, no fleeing. Returns true when the mob died and
    // was removed.
    bool hurt(quint32 mobId, double amount);
    // The same, returning the mob's drops when it died (cramming, decision E-401).
    std::optional<QList<QcItemDrop>> wound(quint32 mobId, double amount);
    // The same for the mob at `index` in mobs(); a death removes it from the list.
    std::optional<QList<QcItemDrop>> woundAt(qsizetype index, double amount);
    // Removes a mob without a death (caught); false when there is none.
    bool remove(quint32 mobId);
    // Takes the mobs `leaves` picks out of the store because their chunk unloads (Departure::Unloaded).
    void unload(const std::function<bool(const QcCreature &mob)> &leaves);
    // Spawns a mob directly (commands, tests); returns its id.
    quint32 spawn(int type, const QcVec3 &position, quint32 id, bool underground = false, bool persistent = false);
    // Clears the store without departures (tests that want an empty world).
    void removeAll() { m_mobs.clear(); }
    // Game rule mobSpawning: without it no creature spawns by itself, neither around players nor
    // with new chunks (decision E-221); /summon and world setups still add them.
    void setNaturalSpawning(bool on) { m_naturalSpawning = on; }
    bool naturalSpawning() const { return m_naturalSpawning; }
    // Game rule mobInvulnerabilityTicks (decision E-429): the game ticks a creature stays
    // invulnerable after a hurt (0: every hit counts). A lower value shortens the open windows at once.
    void setHurtTicks(int ticks);
    int hurtTicks() const { return m_hurtTicks; }

    // Gives a chunk seen for the first time the creatures of the village pieces in it (an animal
    // pen's pair, decision E-404), a villager for each free bed in it (decision E-411) and its group
    // of animals (QcNaturalSpawner::chunkAnimalChance, decided by the seed), and remembers it; later
    // calls for the same chunk do nothing. The chunk must be loaded.
    void populateChunk(QcServerWorld &world, QcChunkPos pos, const std::function<quint32()> &nextEntityId);
    bool isPopulated(QcChunkPos pos) const { return m_populated.contains(pos); }
    const QSet<QcChunkPos> &populatedChunks() const { return m_populated; }
    // The chunk left the world; whether it got its animals goes with it (decision E-374).
    void forgetPopulated(QcChunkPos pos) { m_populated.remove(pos); }
    void notePopulated(QcChunkPos pos) { m_populated.insert(pos); }
    // Persistent mobs to save, and loading them back (as dormant until the next tick sees their chunk).
    QList<Stored> persistentMobs() const;
    static Stored storedOf(const QcCreature &mob);
    void restore(const QList<Stored> &mobs, const QSet<QcChunkPos> &populated, const std::function<quint32()> &nextEntityId);
    // The persistent id the next saved creature gets (QcCreature::uid); world.json keeps it. Setting
    // never lowers it below an id already given.
    quint64 nextUid() const { return m_nextUid; }
    void setNextUid(quint64 next) { m_nextUid = std::max(m_nextUid, next); }
    // Mobs that are not dormant, all of them and those that came by /summon.
    qsizetype awakeCount() const;
    qsizetype awakeSummonedCount() const;
    const QList<QcCreature> &mobs() const { return m_mobs; }
    // For the breeding timers (QcMobBreeding) and the parts that set up a mob they just spawned;
    // never add or remove mobs through it.
    QList<QcCreature> &editableMobs() { return m_mobs; }
    const QcCreature *find(quint32 id) const;
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
