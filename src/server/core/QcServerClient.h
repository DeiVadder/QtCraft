#pragma once

#include "common/content/definitions/QcAppearance.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcExperience.h"
#include "common/gameplay/QcGameMode.h"
#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcStatusEffects.h"
#include "common/protocol/QcConnection.h"
#include "common/protocol/QcMessages.h"
#include "server/core/QcRateLimiter.h"
#include "server/entities/QcEntity.h"
#include "server/entities/QcHurtWindow.h"
#include "server/gameplay/QcHunger.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <memory>
#include <optional>

// Server-internal state of one connected client, shared by the parts of QcServer, grouped by what it
// is for (decision E-367). Its player is an
// entity like the others (decision E-364): id, motion and yaw are the player's; only its inputs move
// it (QcEntityMotion::stepPlayer), and it never rests.
struct QcServerClient : QcEntity
{
    // The kind of every entity of this type (QcEntity::as).
    static constexpr QcEntityKind entityKind = QcEntityKind::Player;

    QcServerClient()
        : QcEntity(QcEntityKind::Player)
    {
    }

    enum class State { AwaitingHello, AwaitingLogin, Playing, Closed };

    // Inputs a client may queue before further inputs are dropped (anti speed-hack); at slow tick
    // rates a whole tick's share of inputs plus inputMargin (decision E-249).
    static constexpr int maxInputBudget = 10;
    static constexpr int inputMargin = 5;
    static constexpr double maxHealth = 20.0;
    // Blocks resent per tick for dropped mining messages (QcServer::resendDroppedMiningTarget).
    static constexpr qsizetype maxMiningResendsPerTick = 3;

    // The login (decision E-252): where the client connects from (address and port of a network
    // client; empty in-process), since when (the login must finish within loginTimeoutMs), the nonce
    // of the challenge and the fingerprint of the proven identity (remote clients).
    struct Login
    {
        QString address;
        QElapsedTimer connected;
        QByteArray nonce;
        QString fingerprint;
    };

    // What the client may send: inputs refilled each tick by the real time it stands for, one per
    // 50 ms (decision E-249), so other tick rates and sprints keep the player's pace; budgets of the
    // other spammable messages; the latest input applied; positions whose true block was resent in
    // tick `miningResendTick` after a dropped mining message.
    struct Input
    {
        double budget = maxInputBudget;
        QcRateLimiter rateLimiter;
        quint32 lastSequence = 0;
        QList<QcBlockPos> miningResends;
        quint64 miningResendTick = 0;
    };

    // What the client has been told about the world: chunks streamed around the player within
    // `distance`, entities it knows (with the stack count of an item, the position of its last
    // move and whether that move came in the last tick, QcEntitySync), known entities whose spawn
    // data changed and must be sent again, and the debug data it subscribed to
    // (QcMsg::DebugSubscribe) and when that was last sent.
    struct SentEntity
    {
        int count = 0;
        QcVec3 position;
        bool moving = false;
    };
    struct View
    {
        int distance = 0;
        QSet<QcChunkPos> sentChunks;
        // Counts changes of sentChunks (QcChunkLoading recomputes the needed chunks only on a change).
        quint64 sentChanges = 0;
        QHash<quint32, SentEntity> known;
        QSet<quint32> stale;
        quint8 debugFlags = 0;
        std::optional<quint64> debugSentTick;
    };

    // The crafting grid, row-major with three cells per row; a 2 x 2 grid uses the top-left four,
    // a crafting table at `table` the whole grid.
    struct Crafting
    {
        QList<QcItemStack> grid = QList<QcItemStack>(9);
        int size = 2;
        std::optional<QcBlockPos> table;
    };

    // The machine, container or villager whose screen the player has open, and what the client was
    // last told about it.
    struct Screen
    {
        std::optional<QcBlockPos> open;
        // A container minecart (entity id) instead of a block; `open` is unset then.
        quint32 cart = 0;
        std::optional<QcMsg::MachineState> sent;
        // The villager (entity id) whose trading screen is open (QcServerTrading), 0 for none, and
        // the screen as last sent.
        quint32 trading = 0;
        std::optional<QcMsg::TradeState> sentTrade;
    };

    // The block being mined, since which tick and players' time (QcServerClock::playerTimeNs).
    struct Mining
    {
        std::optional<QcBlockPos> pos;
        quint64 startTick = 0;
        qint64 startNs = 0;
    };

    std::unique_ptr<QcConnection> connection;
    State state = State::AwaitingHello;
    // Connected through the in-process host (the local single player): exempt from the chat limit,
    // always an operator, no login.
    bool local = false;
    bool joined = false;
    QString name;
    Login login;
    Input input;
    View view;
    Crafting crafting;
    Screen screen;
    Mining mining;

    // The player's body and look; id, motion and yaw are in QcEntity.
    float pitch = 0.0f;
    // Position before the last processed input; actions may have been aimed from there.
    std::optional<QcVec3> previousPosition;
    // The client must get its position again (PlayerState): a correction or an acknowledged input.
    bool stateDirty = false;
    bool sneaking = false;
    bool sprinting = false;
    // Set by actions that swing the arm; reported to other clients in this tick's sync.
    bool swung = false;
    // Drawing the launcher in the hotbar slot drawSlot since the tick drawStart (decision E-425,
    // QcPlayerLaunchers); others see the aim.
    bool drawing = false;
    quint64 drawStart = 0;
    int drawSlot = -1;
    // The tick from which each thrown item may be thrown again (decision E-427, QcPlayerThrows).
    QHash<QcItemId, quint64> throwReady;
    QcAppearance appearance;
    QcInventory inventory;
    // Armour slots; sent to the player and to everyone who sees them.
    QcEquipment equipment;
    // Game mode (decision E-282) and the inventory and armour of the other inventory set, kept
    // aside until the player changes back (decision E-283).
    QcGameMode gameMode = QcGameMode::Survival;
    QcInventory keptInventory;
    QcEquipment keptEquipment;
    QcGameModes::Rules rules() const { return QcGameModes::rules(gameMode); }

    // Health and food (QcPlayerHealth).
    double health = maxHealth;
    bool dead = false;
    // Highest point of the current fall, for fall damage.
    double fallPeak = 0.0;
    QcHunger hunger;
    // Food points last sent to the client; -1 before the first message.
    int sentFood = -1;
    // The short invulnerability after a hurt (game rule playerInvulnerabilityTicks, decision E-429).
    QcHurtWindow hurt;
    double attackCooldown = 0.0;
    // Ticks spent below the void limit in a row (QcPlayerHealth::applyVoidDamage).
    int voidTicks = 0;
    // Set when damage was taken; reported to other clients in this tick's sync.
    bool hurtThisTick = false;
    // The weather last sent to the client (QcServerWeather); none before the first message.
    std::optional<QcMsg::Weather> sentWeather;
    // Experience (QcServerExperience, decision E-398) and what the client last got of it; none before
    // the first message.
    QcExperience experience;
    std::optional<QcExperience> sentExperience;
    // The creatures this player fought lately (decision E-480): the last that hurt them and the last
    // they struck (entity ids, 0: none) with the tick it happened; their pets defend them
    // (QcMobManager::PlayerView::hurtBy and struck).
    struct Foes
    {
        quint32 hurtBy = 0;
        quint64 hurtTick = 0;
        quint32 struck = 0;
        quint64 struckTick = 0;
    };
    Foes foes;
    // Status effects (QcServerEffects, decision E-450) and those last sent with the health, counted
    // down in step with them: the two differ only when an effect came, went, changed its level or
    // was given again for longer.
    QcStatusEffects effects;
    QcStatusEffects sentEffects;
    // Fishing with a rod (QcPlayerFishing, decision E-503; not saved): the bobber cast (entity id,
    // 0: none) from the hotbar slot `slot`, the ticks until a fish bites (-1: not drawn yet, the bobber
    // does not float) and the ticks the bite still lasts.
    struct Fishing
    {
        quint32 bobber = 0;
        int slot = -1;
        int waitTicks = -1;
        int biteTicks = 0;
    };
    Fishing fishing;
};
