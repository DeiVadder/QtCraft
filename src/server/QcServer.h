#pragma once

#include "QcContainerManager.h"
#include "QcContentRegistry.h"
#include "QcFallingBlockManager.h"
#include "QcItemEntityManager.h"
#include "QcLiquidFlow.h"
#include "QcMachineManager.h"
#include "QcMobManager.h"
#include "QcPlantManager.h"
#include "QcMessages.h"
#include "QcServerConfig.h"
#include "QcServerWorld.h"
#include "QcWorldStorage.h"

#include <QObject>
#include <QSet>
#include <QRandomGenerator>

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

class QcConnection;
class QcTransport;
class QTimer;

// Authoritative game server. Owns the world and all game logic and talks to clients only through
// protocol messages. Lives in one thread; all functions must be called from that thread.
class QcServer : public QObject
{
    Q_OBJECT

public:
    QcServer(QcServerConfig config, std::shared_ptr<const QcContentRegistry> content,
             std::unique_ptr<QcWorldGenerator> generator, QObject *parent = nullptr);
    ~QcServer() override;

    // Opens or creates the world directory given in the config (no-op without one). Must succeed
    // before start(); fails with a readable message for damaged or mismatching saves.
    QcStatus openWorld();
    // Writes all players, world info, changed chunks, entities, machines, chests and pending
    // updates. A failing part does not keep the others from being written; the first error is
    // returned. No-op without a world directory.
    QcStatus save();
    void start();
    // Saves, then disconnects every client; returns the result of saving.
    QcStatus stop();
    void addClient(std::unique_ptr<QcTransport> transport);

    int clientCount() const { return int(m_clients.size()); }
    // Ticks of slack when checking a client's claim that a block is broken (network jitter).
    static constexpr int miningToleranceTicks = 2;
    // Inputs a client may queue beyond one per tick before further inputs are dropped (anti speed-hack).
    static constexpr int maxInputBudget = 10;
    static constexpr double playerMaxHealth = 20.0;
    quint64 tickCount() const { return m_tickCount; }
    QcServerWorld &world() { return m_world; }
    QcMobManager &mobs() { return m_mobs; }
    QcItemEntityManager &items() { return m_items; }
    QcPlantManager &plants() { return m_plants; }
    QcMachineManager &machines() { return m_machines; }
    QcContainerManager &containers() { return m_containers; }
    QcLiquidFlow &liquids() { return m_liquids; }
    QcFallingBlockManager &fallingBlocks() { return m_falling; }
    bool keepInventory() const { return m_info.keepInventory; }
    void setKeepInventory(bool keep) { m_info.keepInventory = keep; }
    double worldTime() const { return m_info.worldTime; }
    // Problems found while opening the world (missing mods, unknown blocks); it still opened.
    const QStringList &openWarnings() const { return m_openWarnings; }
    void setWorldTime(double seconds);

    // Players are visible to each other and mobs are sent to clients within this distance.
    static constexpr double entityViewDistance = 64.0;
    // Dropped items are sent to clients within this distance.
    static constexpr double itemViewDistance = 32.0;
    // Item moves smaller than this are not sent (items bobbing on water).
    static constexpr double itemMoveThreshold = 0.02;
    // Speed of items thrown by a player.
    static constexpr double throwSpeed = 6.0;
    // A creature with an ambient sound makes it on average this often (seconds); a burning machine
    // crackles on average this often.
    static constexpr double mobAmbientSeconds = 10.0;
    static constexpr double machineSoundSeconds = 2.5;
    // Melee reach of players, from the eyes to the target's centre.
    static constexpr double attackReach = 4.0;
    static constexpr double playerAttackCooldown = 0.4;
    static constexpr double fallDamageThreshold = 3.0;
    // Crafting needs a crafting table within this distance for 3 x 3 recipes.
    static constexpr double craftingTableReach = 5.0;
    // While mining, the arm swings every this many ticks.
    static constexpr int miningSwingTicks = 6;
    static constexpr int craftingGridSlot = QcMsg::MoveInventoryItem::craftingGridSlot;
    static constexpr int machineSlot = QcMsg::MoveInventoryItem::machineInputSlot;
    const QcServerConfig &config() const { return m_config; }

    // Runs one simulation tick immediately (used by tests and by the tick timer).
    void tick();

    // Neighbour updates (connections, lost support) processed per tick at most; the rest waits for
    // the next tick, so a long chain of falling torches never stalls the server. Updates caused by
    // players go first; those of commands (/fill) and their cascades use what is left.
    static constexpr int maxBlockUpdatesPerTick = 1024;
    // Largest volume one /fill command changes.
    static constexpr qint64 maxFillBlocks = 32768;
    // Updates waiting for their chunk to load at most; loading a chunk also re-checks every block on
    // its borders that connects or hangs across them, so dropping beyond this loses nothing.
    static constexpr qsizetype maxWaitingBlockUpdates = 65536;
    // Queued updates of loaded chunks (the next ticks work on them).
    qsizetype pendingBlockUpdates() const { return qsizetype(m_playerUpdates.size()) + qsizetype(m_commandUpdates.size()); }
    // Updates of positions in chunks that are not loaded.
    qsizetype waitingBlockUpdates() const { return m_waitingUpdateCount; }
    // Updates waiting in the queue of player actions and in the queue of commands.
    qsizetype pendingPlayerBlockUpdates() const { return qsizetype(m_playerUpdates.size()); }
    qsizetype pendingCommandBlockUpdates() const { return qsizetype(m_commandUpdates.size()); }
    quint64 processedBlockUpdates() const { return m_processedBlockUpdates; }
    // Command updates dropped because the command queue was full (config.maxCommandBlockUpdates).
    quint64 droppedBlockUpdates() const { return m_droppedBlockUpdates; }

    // Wall time spent in tick(); safe to read and reset from other threads (benchmark).
    struct TickTimes
    {
        double averageMs = 0.0;
        double worstMs = 0.0;
        quint64 ticks = 0;
    };
    TickTimes tickTimes() const;
    void resetTickTimes();

signals:
    void clientCountChanged(int count);

private:
    struct Client;

    void handleMessage(Client &client, QcMessage &&message);
    void handleHello(Client &client, const QcMsg::Hello &hello);
    void handleInput(Client &client, const QcMsg::PlayerInput &message);
    void handleFinishMining(Client &client, QcBlockPos pos);
    void handleUseItem(Client &client, const QcMsg::UseItem &message);
    void handleChat(Client &client, const QString &text);
    bool isWithinReach(const Client &client, QcBlockPos pos) const;
    // Sets the block, tells clients and queues its neighbours for an update.
    void setBlockAndBroadcast(QcBlockPos pos, QcBlockId block);
    // Queues the position itself (unless it became air) and its six neighbours.
    void queueNeighbourUpdates(QcBlockPos pos, bool self);
    void queueBlockUpdate(QcBlockPos pos);
    // Re-derives connections and breaks blocks that lost their support, within the tick budget.
    // Positions in unloaded chunks wait until the chunk loads again.
    void processBlockUpdates();
    // Re-queues waiting updates of chunks that loaded since the last tick and the blocks along their
    // borders that connect or hang across them; reconciles chests with the loaded blocks.
    void handleLoadedChunks();
    void queueBorderUpdates(QcChunkPos chunk, QcBlockFace towards);
    // Remembers an update of `pos` until `chunk` loads.
    void waitForChunk(QcChunkPos chunk, QcBlockPos pos);
    // Runs the due flow steps of liquids within their budget (decision E-054).
    void processLiquids();
    // Players and mobs touching a damaging block (lava) get hurt; items in it burn.
    void applyContactDamage();
    // Empties a liquid source into the held scooping item; false when nothing was taken.
    bool scoopLiquid(Client &client, QcBlockPos pos);
    void handleUseEntity(Client &client, quint32 entityId);
    // Lets the block at `pos` fall: as an entity, or straight to its resting cell when too many fall.
    void startFalling(QcBlockPos pos, QcBlockId block);
    // Puts a block that came to rest into its cell, or drops it as items.
    void landBlock(QcBlockId block, QcBlockPos cell, bool placed);
    void tickFallingBlocks();
    // Turns one item of the selected stack into `item` (filled bucket, firefly in a bottle); what
    // does not fit into the inventory is thrown.
    void exchangeHeld(Client &client, const QString &item);
    // Opens or closes a door, trapdoor or gate (both halves of a door), unless the changed shape
    // would close on a player or mob.
    void toggleBlock(QcBlockPos pos, QcBlockId block);
    // Whether any of `boxes` (world space) overlaps a playing player or a mob.
    bool overlapsBody(const QList<QcAabb> &boxes) const;
    // Removes a block as if broken: drops its items (when `drops` is set) and lets dependent blocks
    // react (leaves decay, unsupported saplings break). The other half of a door breaks with it
    // under the same drop decision.
    void breakBlock(QcBlockPos pos, bool drops);
    // Removes chests whose block is no longer a storage block (removed mods) and fits the others to
    // their block's size; spilled contents drop as items.
    void reconcileContainer(QcBlockPos pos);
    void dropBlockItems(const QcBlockDefinition &definition, QcBlockPos pos);
    // Sends a sound to every playing client within the sound's range (its definition's range, longer
    // for louder events). Unknown sounds are not sent.
    void playSound(const QString &key, const QcVec3 &position, float volume = 1.0f, float pitch = 1.0f);
    void playBlockSound(const QcBlockDefinition &definition, QcBlockSoundAction action, QcBlockPos pos);
    // Spawns the stack as an item entity thrown from the player's eyes along the view direction.
    void throwFromPlayer(const Client &client, QcItemStack stack);
    void handleDropItem(Client &client, QcMsg::DropItem message);
    // Replaces unknown outfits and hair styles with defaults; names come from the content.
    QcAppearance validAppearance(const QcAppearance &requested) const;
    void handleSetAppearance(Client &client, const QcAppearance &appearance);
    void dropInventory(Client &client);
    // Moves the crafting grid into the inventory; what does not fit is thrown on the ground.
    void returnCraftingGrid(Client &client);
    // Puts a despawning sapling item into the ground when there is room.
    void tryPlant(const QcItemEntityManager::Item &item);
    bool isLoadedAt(QcBlockPos pos) const;
    void tickItemsAndPlants();
    QcStatus saveEntities();
    void sendInventory(Client &client);
    void sendOpenWarnings(Client &client);
    const QcItemDefinition *heldItem(const Client &client) const;
    QcVec3 spawnPosition();
    void handleAttack(Client &client, quint32 entityId);
    void handleRespawn(Client &client);
    void damagePlayer(Client &client, double amount, const QcVec3 &knockback);
    void updateHealth(Client &client, bool wasOnGround, bool wasInWater, double previousY);
    void syncEntities(Client &client);
    void sendHealth(Client &client);
    void sendTime(Client &client);
    void tickGameplay();
    void handleMoveItem(Client &client, QcMsg::MoveInventoryItem move);
    void handleTakeCraftingResult(Client &client);
    void handleCraftRecipe(Client &client, const QcMsg::CraftRecipe &request);
    void closeCrafting(Client &client);
    void handleConsume(Client &client);
    void sendFood(Client &client);
    // Opens the machine or chest screen of the block at `pos` for the player.
    void openMachine(Client &client, QcBlockPos pos);
    // The open chest of the player while it still exists, else null.
    QcContainerManager::Container *openContainerOf(const Client &client);
    void dropStacks(const QList<QcItemStack> &stacks, QcBlockPos pos);
    // Forgets the open machine; `notify` tells the client (the server closed it).
    void closeMachine(Client &client, bool notify);
    // The open machine of the player while it still exists, else null.
    QcMachineManager::Machine *openMachineOf(const Client &client);
    // Sends the open machine's state if it changed since the last message (or always with `force`).
    void sendMachine(Client &client, bool force = false);
    void dropMachineContents(const QcMachineManager::Machine &machine);
    void tickMachines();
    // The grid of the current size as a size x size list, and the recipe it makes.
    QList<QcItemStack> craftingView(const Client &client) const;
    std::optional<qsizetype> craftingMatch(const Client &client) const;
    bool isNearCraftingTable(const Client &client) const;
    void streamChunks(Client &client);
    void removeClosedClients();
    QcStatus savePlayer(const Client &client);

    QcServerConfig m_config;
    std::shared_ptr<const QcContentRegistry> m_content;
    QcServerWorld m_world;
    std::vector<std::unique_ptr<Client>> m_clients;
    QTimer *m_tickTimer = nullptr;
    quint64 m_tickCount = 0;
    std::atomic<qint64> m_tickNsTotal{0};
    std::atomic<qint64> m_tickNsWorst{0};
    std::atomic<quint64> m_ticksTimed{0};
    quint32 m_nextEntityId = 1;
    std::optional<QcWorldStorage> m_storage;
    QcWorldInfo m_info;
    QcMobManager m_mobs;
    QcItemEntityManager m_items;
    QcPlantManager m_plants;
    QcMachineManager m_machines;
    QcContainerManager m_containers;
    QcLiquidFlow m_liquids;
    QcFallingBlockManager m_falling;
    // Spreading liquids make at most one sound per tick.
    bool m_liquidSoundPlayed = false;
    QRandomGenerator m_random;
    // Separate from m_random so that sounds never change drops or spawns.
    QRandomGenerator m_soundRandom;
    // Entities that died this tick; their removal tells clients to play the death animation.
    QSet<quint32> m_died;
    QStringList m_openWarnings;
    std::deque<QcBlockPos> m_playerUpdates;
    std::deque<QcBlockPos> m_commandUpdates;
    QSet<QcBlockPos> m_queuedPlayerUpdates;
    QSet<QcBlockPos> m_queuedCommandUpdates;
    // Updates of positions whose chunk was not loaded when their turn came.
    QHash<QcChunkPos, QSet<QcBlockPos>> m_waitingUpdates;
    qsizetype m_waitingUpdateCount = 0;
    // While set, new updates go to the command queue (a command runs or a command update cascades).
    bool m_commandUpdateMode = false;
    quint64 m_processedBlockUpdates = 0;
    quint64 m_droppedBlockUpdates = 0;
};
