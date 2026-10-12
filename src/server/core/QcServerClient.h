#pragma once

#include "QcConnection.h"
#include "QcEntityMotion.h"
#include "QcAppearance.h"
#include "QcEquipment.h"
#include "QcGameMode.h"
#include "QcHunger.h"
#include "QcInventory.h"
#include "QcMessages.h"
#include "QcRateLimiter.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <memory>
#include <optional>

// Server-internal state of one connected client, shared by the parts of QcServer.
struct QcServerClient
{
    enum class State { AwaitingHello, AwaitingLogin, Playing, Closed };

    // Inputs a client may queue before further inputs are dropped (anti speed-hack); at slow tick
    // rates a whole tick's share of inputs plus inputMargin (decision E-249).
    static constexpr int maxInputBudget = 10;
    static constexpr int inputMargin = 5;
    static constexpr double maxHealth = 20.0;
    // Blocks resent per tick for dropped mining messages (QcServer::resendDroppedMiningTarget).
    static constexpr qsizetype maxMiningResendsPerTick = 3;

    std::unique_ptr<QcConnection> connection;
    State state = State::AwaitingHello;
    quint32 entityId = 0;
    QString name;
    QcEntityMotion motion;
    // Position before the last processed input; actions may have been aimed from there.
    std::optional<QcVec3> previousPosition;
    // Refilled each tick by the real time it stands for, one input per 50 ms (decision E-249), so
    // other tick rates and sprints keep the player's pace.
    double inputBudget = maxInputBudget;
    // Budgets of the other spammable messages.
    QcRateLimiter rateLimiter;
    // Connected through the in-process host (the local single player): exempt from the chat limit,
    // always an operator, no login.
    bool local = false;
    // Where the client connects from (address and port of a network client; empty in-process).
    QString address;
    // Since the connection was accepted; the login must finish within loginTimeoutMs.
    QElapsedTimer connected;
    // The nonce of the login challenge, and the fingerprint of the proven identity (remote clients).
    QByteArray loginNonce;
    QString fingerprint;
    // Chunks streamed around this player.
    int viewDistance = 0;
    // Positions whose true block was resent in tick `miningResendTick` after a dropped mining message.
    QList<QcBlockPos> miningResends;
    quint64 miningResendTick = 0;
    quint32 lastInputSequence = 0;
    bool stateDirty = false;
    QSet<QcChunkPos> sentChunks;
    QcInventory inventory;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool joined = false;
    std::optional<QcBlockPos> miningPos;
    quint64 miningStartTick = 0;
    // Players' time (QcServerState::playerTimeNs) when the mining started.
    qint64 miningStartNs = 0;

    double health = maxHealth;
    bool dead = false;
    // Highest point of the current fall, for fall damage.
    double fallPeak = 0.0;
    QcHunger hunger;
    // Food points last sent to the client; -1 before the first message.
    int sentFood = -1;
    double hurtTimer = 0.0;
    double attackCooldown = 0.0;
    // Ticks spent below the void limit in a row (QcServerCombat::applyVoidDamage).
    int voidTicks = 0;
    // Entities this client has been told about.
    QSet<quint32> knownEntities;
    // Known entities whose spawn data changed and must be sent again.
    QSet<quint32> staleEntities;
    // Last stack count and position sent for each visible item entity.
    struct ItemView
    {
        int count = 0;
        QcVec3 position;
    };
    QHash<quint32, ItemView> knownItems;
    // Crafting grid, row-major with three cells per row; a 2 x 2 grid uses the top-left four.
    QList<QcItemStack> craftingGrid = QList<QcItemStack>(9);
    int craftingSize = 2;
    std::optional<QcBlockPos> craftingTable;
    bool sneaking = false;
    bool sprinting = false;
    // Set by actions that swing the arm; reported to other clients in this tick's sync.
    bool swung = false;
    // Set when damage was taken; reported to other clients in this tick's sync.
    bool hurtThisTick = false;
    QcAppearance appearance;
    // Armour slots (stage 23 fills them); sent to the player and to everyone who sees them.
    QcEquipment equipment;
    // Game mode (decision E-282) and the inventory and armour of the other inventory set, kept
    // aside until the player changes back (decision E-283).
    QcGameMode gameMode = QcGameMode::Survival;
    QcInventory keptInventory;
    QcEquipment keptEquipment;
    QcGameModes::Rules rules() const { return QcGameModes::rules(gameMode); }
    // The machine whose screen the player has open, and what the client was last told about it.
    std::optional<QcBlockPos> machine;
    std::optional<QcMsg::MachineState> sentMachine;
    // Debug data the client subscribed to (QcMsg::DebugSubscribe) and when it was last sent.
    quint8 debugFlags = 0;
    std::optional<quint64> debugSentTick;
};
