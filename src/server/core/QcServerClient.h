#pragma once

#include "QcConnection.h"
#include "QcEntityMotion.h"
#include "QcAppearance.h"
#include "QcHunger.h"
#include "QcInventory.h"
#include "QcMessages.h"
#include "QcRateLimiter.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <memory>
#include <optional>

// Server-internal state of one connected client, shared by the parts of QcServer.
struct QcServerClient
{
    enum class State { AwaitingHello, Playing, Closed };

    // Inputs a client may queue beyond one per tick before further inputs are dropped (anti speed-hack).
    static constexpr int maxInputBudget = 10;
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
    int inputBudget = maxInputBudget;
    // Budgets of the other spammable messages.
    QcRateLimiter rateLimiter;
    // Connected through the in-process host (the local single player); exempt from the chat limit.
    bool local = false;
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

    double health = maxHealth;
    bool dead = false;
    // Highest point of the current fall, for fall damage.
    double fallPeak = 0.0;
    QcHunger hunger;
    // Food points last sent to the client; -1 before the first message.
    int sentFood = -1;
    double hurtTimer = 0.0;
    double attackCooldown = 0.0;
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
    // The machine whose screen the player has open, and what the client was last told about it.
    std::optional<QcBlockPos> machine;
    std::optional<QcMsg::MachineState> sentMachine;
};
