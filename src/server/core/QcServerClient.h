// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcAppearance.h"
#include "common/gameplay/QcAdvancementProgress.h"
#include "common/gameplay/QcEnchantingTable.h"
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
    // client; empty in-process), the program it reports, since when (the login must finish within loginTimeoutMs), the nonce
    // of the challenge and the fingerprint of the proven identity (remote clients).
    struct Login
    {
        QString address;
        // What the client said it is (QcMsg::Hello::client), for the log.
        QString client;
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

    // The open stonecutter (QcServerStonecutter, decision E-785): where it stands, the stack in its
    // input slot and the stonecutting recipe chosen for it (empty: none). Its output is not stored; it
    // follows from both (QcStonecutting::output).
    struct Stonecutter
    {
        QcBlockPos pos;
        QcItemStack input;
        QString recipe;

        // Puts `stack` into the input slot; another item or none drops the chosen recipe, as in the
        // reference (more or fewer of the same item keep it).
        void setInput(const QcItemStack &stack)
        {
            if (stack.isEmpty() || stack.item != input.item) {
                recipe.clear();
            }
            input = stack.isEmpty() ? QcItemStack() : stack;
        }
    };

    // The open enchanting table (QcServerEnchanting, decision E-864): where it stands and the stacks in
    // its item and lapis slots. Its offers follow from the item, the bookshelves and the player's
    // enchantment seed. `shown` and `shownShelves` are what the client was last told (stateOf):
    // the player takes only an offer he saw, and a changed shelf count sends the offers again.
    struct Enchanting
    {
        QcBlockPos pos;
        QcItemStack item;
        QcItemStack lapis;
        QcEnchantingTable::Offers shown{};
        int shownShelves = -1;
    };

    // The open anvil (QcServerAnvil, decision E-866): where it stands, its left and right slots and
    // the text of its name field. What it makes follows from them (QcAnvil::result). The name field
    // follows the left stack: whenever another stack (or the same with other data) lies there, the
    // field holds that stack's custom name, so a repair keeps the name (as in the reference).
    struct Anvil
    {
        QcBlockPos pos;
        QcItemStack left;
        QcItemStack right;
        QString name;

        void setLeft(const QcItemStack &stack)
        {
            if (stack.item != left.item || !(stack.itemData() == left.itemData())) {
                name = stack.name();
            }
            left = stack;
        }
    };

    // The machine, container or villager whose screen the player has open, and what the client was
    // last told about it.
    // A portable container opened from the hand (a backpack, decision E-833): the hotbar slot that
    // holds it, locked while open, and its slots as a working copy of the item's contents, filled
    // from the item whenever the screen is used (QcBlockScreens::openContainerOf) and written back
    // into it by QcServerInventory.
    struct HeldContainer
    {
        int slot = 0;
        QList<QcItemStack> stacks;
    };

    struct Screen
    {
        std::optional<QcBlockPos> open;
        // A container minecart (entity id) instead of a block; `open` is unset then.
        quint32 cart = 0;
        // A backpack opened from the hand instead of a block or cart; `open` is unset and `cart` 0 then.
        std::optional<HeldContainer> held;
        std::optional<QcMsg::MachineState> sent;
        // The villager (entity id) whose trading screen is open (QcServerTrading), 0 for none, and
        // the screen as last sent.
        quint32 trading = 0;
        std::optional<QcMsg::TradeState> sentTrade;
        // The stonecutter whose screen is open. It, a machine, chest or cart (`open`, `cart`) and a
        // trade (`trading`) are never open at once: opening one closes the others
        // (QcServerStonecutter::open, QcBlockScreens::open/openCart, QcServerTrading::open).
        std::optional<Stonecutter> stonecutter;
        // The enchanting table whose screen is open; never together with any of the above.
        std::optional<Enchanting> enchanting;
        // The anvil whose screen is open; never together with any of the above.
        std::optional<Anvil> anvil;
        // The sign whose editor the server opened for the player (QcServerSigns, decision E-766).
        std::optional<QcBlockPos> sign;
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
    // The player has the pause menu open (QcMsg::PauseRequest, decision E-877); only a local
    // player's wish pauses the server (QcServerPause).
    bool pauseRequested = false;
    // A network client's pause request was refused in the log already (once per connection).
    bool pauseRefusalLogged = false;
    QString name;
    // The round trip the player list shows for the client (decision E-886), in microseconds; -1
    // while unknown. A display value: the client's reports replace it only when the change shows
    // (QcServerState::reportLatency).
    qint32 listedLatencyUs = -1;
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
    // The highest prediction sequence (decision E-853) of the actions handled since the last world
    // sync; acknowledged after it (BlockAck), so the client knows the drops and other entities of
    // the actions by then. 0: nothing to acknowledge.
    quint32 pendingBlockAck = 0;
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
    // Whether the player's body is in the world: playing, alive and no spectator (decision E-730).
    // Only such a body is seen by others, noticed by creatures, hit, pushed and pressing plates.
    [[nodiscard]] bool hasBody() const { return state == State::Playing && !dead && rules().tangible; }

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
    // Fixes the enchanting table's offers until the player enchants (decision E-864); saved with the
    // player, 0 until the server draws one.
    quint32 enchantSeed = 0;
    // Orders the runes of the enchanting screen (decision E-884): drawn on its own whenever
    // enchantSeed is, so the client learns nothing about the offers from it; saved with the player.
    quint32 runeSeed = 0;
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
    // The adventure guide (QcServerAdvancements, decision E-923; `progress` is saved): the met
    // criteria, the advancements whose change the client has not heard of yet (with whether to
    // announce them), whether it got the whole progress since joining, the block where the
    // location criteria were last checked, and the chat lines about his reached ones this tick
    // (decision E-924): those sent and those beyond the limit, folded into one line at its end.
    struct Advancements
    {
        QcAdvancementProgress progress;
        QHash<QString, bool> unsent;
        bool synced = false;
        std::optional<QcBlockPos> checkedAt;
        int announced = 0;
        int folded = 0;
    };
    Advancements advancements;
};
