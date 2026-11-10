// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcAppearance.h"
#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/gameplay/QcAdvancementProgress.h"
#include "common/gameplay/QcAirSupply.h"
#include "common/gameplay/QcEnchantingTable.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcExperience.h"
#include "common/gameplay/QcGameMode.h"
#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcStatusEffects.h"
#include "common/protocol/QcConnection.h"
#include "common/protocol/QcMessages.h"
#include "server/core/QcPlayerScreen.h"
#include "server/core/QcRateLimiter.h"
#include "server/entities/QcEntity.h"
#include "server/entities/QcHurtWindow.h"
#include "server/gameplay/QcHunger.h"
#include "server/gameplay/QcMovementEpochs.h"
#include "server/storage/QcPlayerReturn.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <memory>
#include <optional>

/*! \brief Server-internal state of one connected client, shared by the parts of QcServer, grouped
    by what it is for (decision E-367).

    Its player is an entity like the others (decision E-364): id, motion and yaw are the
    player's; only its inputs move it (QcEntityMotion::stepPlayer), and it never rests. */
struct QcServerClient : QcEntity
{
    /*! The kind of every entity of this type (QcEntity::as). */
    static constexpr QcEntityKind entityKind = QcEntityKind::Player;

    QcServerClient()
        : QcEntity(QcEntityKind::Player)
    {
    }

    /*! Where the client stands in its connection. */
    enum class State {
        /*! Connected, the Hello has not come yet. */
        AwaitingHello,
        /*! The LoginChallenge went out, the answer has not come yet. */
        AwaitingLogin,
        /*! Logged in and playing. */
        Playing,
        /*! The connection is closed; the client is removed. */
        Closed
    };

    /*! Inputs a client may queue before further inputs are dropped (anti speed-hack); at slow
        tick rates a whole tick's share of inputs plus inputMargin (decision E-249). \qcshared */
    static constexpr int maxInputBudget = 10;
    static constexpr int inputMargin = 5;
    /*! The health of a player at full health. */
    static constexpr double maxHealth = 20.0;
    /*! Blocks resent per tick for dropped mining messages (QcServer::resendDroppedMiningTarget). */
    static constexpr qsizetype maxMiningResendsPerTick = 3;

    /*! \brief The login (decision E-252): where the client connects from (address and port of a
        network client; empty in-process), the program it reports, since when (the login must
        finish within loginTimeoutMs), the nonce of the challenge and the fingerprint of the
        proven identity (remote clients). \qcfields */
    struct Login
    {
        QString address;
        /*! What the client said it is (QcMsg::Hello::client), for the log. */
        QString client;
        QElapsedTimer connected;
        QByteArray nonce;
        QString fingerprint;
    };

    /*! \brief What the client may send: inputs refilled each tick by the real time it stands
        for, one per 50 ms (decision E-249), so other tick rates and sprints keep the player's
        pace; budgets of the other spammable messages; the latest input applied; positions whose
        true block was resent in tick \c {miningResendTick} after a dropped mining message.
        \qcfields */
    struct Input
    {
        double budget = maxInputBudget;
        QcRateLimiter rateLimiter;
        quint32 lastSequence = 0;
        QList<QcBlockPos> miningResends;
        quint64 miningResendTick = 0;
    };

    /*! \brief What a client knows of an entity: the stack count of an item, the position of
        its last move and whether that move came in the last tick (QcEntitySync). \qcfields */
    struct SentEntity
    {
        int count = 0;
        QcVec3 position;
        bool moving = false;
    };
    /*! \brief What the client has been told about the world: chunks streamed around the player
        within \c {distance}, entities it knows (SentEntity), known entities whose spawn data
        changed and must be sent again, and the debug data it subscribed to
        (QcMsg::DebugSubscribe) and when that was last sent, and the boss bars it shows by the
        entity id of their creature (QcServerBossBars). \qcfields */
    struct View
    {
        int distance = 0;
        QSet<QcChunkPos> sentChunks;
        /*! Counts changes of sentChunks (QcChunkLoading recomputes the needed chunks only on a
            change). */
        quint64 sentChanges = 0;
        QHash<quint32, SentEntity> known;
        QSet<quint32> stale;
        quint8 debugFlags = 0;
        std::optional<quint64> debugSentTick;
        QHash<quint32, QcMsg::BossBar> bossBars;
    };

    /*! \brief The crafting grid, row-major with three cells per row; a 2 x 2 grid uses the
        top-left four, a crafting table at \c {table} the whole grid. \qcfields */
    struct Crafting
    {
        QList<QcItemStack> grid = QList<QcItemStack>(9);
        int size = 2;
        std::optional<QcBlockPos> table;
    };

    /*! The open stonecutter (QcOpenStonecutter). */
    using Stonecutter = QcOpenStonecutter;
    /*! The open enchanting table (QcOpenEnchanting). */
    using Enchanting = QcOpenEnchanting;
    /*! The open anvil (QcOpenAnvil). */
    using Anvil = QcOpenAnvil;
    /*! The open smithing table (QcOpenSmithing). */
    using Smithing = QcOpenSmithing;
    /*! The open beacon (QcOpenBeacon). */
    using Beacon = QcOpenBeacon;
    /*! A portable container opened from the hand (QcHeldContainer). */
    using HeldContainer = QcHeldContainer;
    /*! The screens the player has open (QcPlayerScreen). */
    using Screen = QcPlayerScreen;

    /*! \brief The block being mined, since which tick and players' time
        (QcServerClock::playerTimeNs). \qcfields */
    struct Mining
    {
        std::optional<QcBlockPos> pos;
        quint64 startTick = 0;
        qint64 startNs = 0;
        /*! The factor of the status effects on the mining speed when it began. */
        double startFactor = 1.0;
    };

    /*! The connection to the client. */
    std::unique_ptr<QcConnection> connection;
    /*! Where the client stands in its connection. */
    State state = State::AwaitingHello;
    /*! Connected through the in-process host (the local single player): exempt from the chat
        limit, always an operator, no login. */
    bool local = false;
    /*! The player joined the world (restored or spawned, decision E-367). */
    bool joined = false;
    /*! The player has the pause menu open (QcMsg::PauseRequest, decision E-877); only a local
        player's wish pauses the server (QcServerPause). */
    bool pauseRequested = false;
    /*! A network client's pause request was refused in the log already (once per connection). */
    bool pauseRefusalLogged = false;
    /*! The player's name. */
    QString name;
    /*! The round trip the player list shows for the client (decision E-886), in microseconds; -1
        while unknown. A display value: the client's reports replace it only when the change
        shows (QcServerState::reportLatency). */
    qint32 listedLatencyUs = -1;
    /*! The login, the input budgets, what the client was told, the crafting grid, the open
        screens and the block being mined (see their types). \qcshared */
    Login login;
    Input input;
    View view;
    Crafting crafting;
    Screen screen;
    Mining mining;

    /*! The player's body and look; id, motion and yaw are in QcEntity. */
    float pitch = 0.0f;
    /*! Position before the last processed input; actions may have been aimed from there. */
    std::optional<QcVec3> previousPosition;
    /*! The client must get its position again (PlayerState): a correction or an acknowledged
        input. */
    bool stateDirty = false;
    /*! The highest prediction sequence (decision E-853) of the actions handled since the last
        world sync; acknowledged after it (BlockAck), so the client knows the drops and other
        entities of the actions by then. 0: nothing to acknowledge. */
    quint32 pendingBlockAck = 0;
    /*! Whether the player sneaks and sprints (his latest input). \qcshared */
    bool sneaking = false;
    bool sprinting = false;
    /*! Whether the player sneaked on the input before the latest one: the server still accepts an
        aim from the crouched eyes then (QcLineOfSight, decision E-961). */
    bool sneakedBefore = false;
    /*! Set by actions that swing the arm; reported to other clients in this tick's sync. */
    bool swung = false;
    /*! Drawing the launcher in the hotbar slot drawSlot since the tick drawStart (decision
        E-425, QcPlayerLaunchers); others see the aim. \qcshared */
    bool drawing = false;
    quint64 drawStart = 0;
    int drawSlot = -1;
    /*! The tick from which each thrown item may be thrown again (decision E-427,
        QcPlayerThrows). */
    QHash<QcItemId, quint64> throwReady;
    /*! The player's look: skin, cape and the rest others see. */
    QcAppearance appearance;
    /*! The hotbar and the main inventory. */
    QcInventory inventory;
    /*! Armour slots; sent to the player and to everyone who sees them. */
    QcEquipment equipment;
    /*! Game mode (decision E-282) and the inventory and armour of the other inventory set, kept
        aside until the player changes back (decision E-283). \qcshared */
    QcGameMode gameMode = QcGameMode::Survival;
    QcInventory keptInventory;
    QcEquipment keptEquipment;
    /*! Returns the rules of the player's game mode. */
    QcGameModes::Rules rules() const { return QcGameModes::rules(gameMode); }
    /*! Returns whether the player's body is in the world: playing, alive and no spectator
        (decision E-730). Only such a body is seen by others, noticed by creatures, hit, pushed
        and pressing plates. */
    [[nodiscard]] bool hasBody() const { return state == State::Playing && !dead && rules().tangible; }

    /*! The dimension the player is in (decision E-978), by name; saved in his file. */
    QString dimension = QString::fromLatin1(QcDimensionDefinition::overworldName);
    /*! Where he returns to when the dimension he was in can open again (decision E-989). */
    std::optional<QcPlayerReturn> returnTo;
    /*! The soul anchor he respawns at (decision E-1112); saved in his file. */
    std::optional<QcRespawnAnchor> anchor;

    /*! Health points and whether the player is dead (QcPlayerHealth). \qcshared */
    double health = maxHealth;
    bool dead = false;
    /*! Highest point of the current fall, for fall damage. */
    double fallPeak = 0.0;
    /*! Food, saturation and exhaustion (QcPlayerHealth). */
    QcHunger hunger;
    /*! Food points last sent to the client; -1 before the first message. */
    int sentFood = -1;
    /*! The short invulnerability after a hurt (game rule playerInvulnerabilityTicks, decision
        E-429). */
    QcHurtWindow hurt;
    /*! Seconds until an attack deals full damage again. */
    double attackCooldown = 0.0;
    /*! Air under water (QcDrowning, decision E-955; saved) and what the client last heard of it;
        none before the first message. \qcshared */
    QcAirSupply air;
    std::optional<QcMsg::PlayerAir> sentAir;
    /*! Ticks spent below the void limit in a row (QcPlayerHealth::applyVoidDamage). */
    int voidTicks = 0;
    /*! Set when damage was taken; reported to other clients in this tick's sync. */
    bool hurtThisTick = false;
    /*! The weather last sent to the client (QcServerWeather); none before the first message. */
    std::optional<QcMsg::Weather> sentWeather;
    /*! The atmosphere last sent to the client (QcServerWeather, decision E-1120); none before the
        first message. */
    std::optional<QcMsg::Atmosphere> sentAtmosphere;
    /*! Experience (QcServerExperience, decision E-398). */
    QcExperience experience;
    /*! Fixes the enchanting table's offers until the player enchants (decision E-864); saved
        with the player, 0 until the server draws one. */
    quint32 enchantSeed = 0;
    /*! Orders the runes of the enchanting screen (decision E-884): drawn on its own whenever
        enchantSeed is, so the client learns nothing about the offers from it; saved with the
        player. */
    quint32 runeSeed = 0;
    /*! The experience the client last got; none before the first message. */
    std::optional<QcExperience> sentExperience;
    /*! \brief The creatures this player fought lately (decision E-480): the last that hurt them
        and the last they struck (entity ids, 0: none) with the tick it happened; their pets
        defend them (QcMobManager::PlayerView::hurtBy and struck). \qcfields */
    struct Foes
    {
        quint32 hurtBy = 0;
        quint64 hurtTick = 0;
        quint32 struck = 0;
        quint64 struckTick = 0;
    };
    /*! The creatures this player fought lately. */
    Foes foes;
    /*! Status effects (QcServerEffects, decision E-450) and those last sent with the health,
        counted down in step with them: the two differ only when an effect came, went, changed
        its level or was given again for longer. \qcshared */
    QcStatusEffects effects;
    QcStatusEffects sentEffects;
    /*! What the status effects last told did to his moving, by the count inputs quote (decision
        E-992). */
    QcMovementEpochs movementEpochs;
    /*! The extra health left of the absorption effect (decision E-992): blows take it first. */
    double absorption = 0.0;
    /*! \brief Fishing with a rod (QcPlayerFishing, decision E-503; not saved): the bobber cast
        (entity id, 0: none) from the hotbar slot \c {slot}, the ticks until a fish bites (-1:
        not drawn yet, the bobber does not float) and the ticks the bite still lasts. \qcfields */
    struct Fishing
    {
        quint32 bobber = 0;
        int slot = -1;
        int waitTicks = -1;
        int biteTicks = 0;
    };
    /*! Fishing with a rod. */
    Fishing fishing;
    /*! \brief The adventure guide (QcServerAdvancements, decision E-923; \c {progress} is
        saved): the met criteria, the advancements whose change the client has not heard of yet
        (with whether to announce them), whether it got the whole progress since joining, the
        block where the location criteria were last checked, and the chat lines about his reached
        ones this tick (decision E-924): those sent and those beyond the limit, folded into one
        line at its end. \qcfields */
    struct Advancements
    {
        QcAdvancementProgress progress;
        QHash<QString, bool> unsent;
        bool synced = false;
        std::optional<QcBlockPos> checkedAt;
        int announced = 0;
        int folded = 0;
    };
    /*! The adventure guide's progress and what the client has heard of it. */
    Advancements advancements;
};
