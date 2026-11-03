// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/admin/QcClientAdmin.h"
#include "clientcore/camera/QcViewCamera.h"
#include "clientcore/figures/QcEntityPose.h"
#include "clientcore/player/QcBlockPrediction.h"
#include "clientcore/player/QcClientAdvancements.h"
#include "clientcore/player/QcClientGameMode.h"
#include "clientcore/player/QcClientInventory.h"
#include "clientcore/player/QcClientVitals.h"
#include "clientcore/player/QcHandActions.h"
#include "clientcore/player/QcHandTimers.h"
#include "clientcore/player/QcLocalAnimation.h"
#include "clientcore/player/QcPrediction.h"
#include "clientcore/player/QcRefusalThrottle.h"
#include "clientcore/session/QcClientAnvil.h"
#include "clientcore/session/QcClientDebugSubscription.h"
#include "clientcore/session/QcClientEnchanting.h"
#include "clientcore/session/QcClientEntities.h"
#include "clientcore/session/QcClientLogin.h"
#include "clientcore/session/QcClientMachineScreen.h"
#include "clientcore/session/QcClientSignEditor.h"
#include "clientcore/session/QcClientSounds.h"
#include "clientcore/session/QcClientStonecutter.h"
#include "clientcore/session/QcClientTrading.h"
#include "clientcore/session/QcSessionState.h"
#include "clientcore/session/QcWorldMirror.h"
#include "clientcore/tick/QcSessionEvent.h"
#include "clientcore/tick/QcSessionTickMode.h"
#include "common/diagnostics/QcRoundTripStats.h"
#include "common/physics/QcPlayerInput.h"
#include "common/protocol/QcMessages.h"

#include <QHash>
#include <QList>
#include <QObject>

#include <memory>
#include <optional>

class QcSessionLink;
class QcSessionThread;
class QcLinkStats;
class QcTickTiming;
class QcTransport;

/*! \brief Client side of one connection: distributes the server's messages to its parts (the world
    mirror QcWorldMirror, the entity mirror, the screens), drives the local player's parts
    (clientcore/player: prediction, hand, animation, inventory) once per tick and sends their
    requests.

    GUI-free; the renderer, the UI and the control interface all observe a session. Its getters
    hand out the parts themselves; the session keeps what combines them, the player's actions and
    the signals (decisions E-530, E-717). Connection, ticks and prediction run in a tick core
    (QcSessionLink, E-560), timer-driven on a thread of its own; the session applies the core's
    ordered events in its own thread, between two frames. */
class QcClientSession : public QObject
{
    Q_OBJECT

public:
    /*! The phase of the connection (QcSessionState); stateChanged() reports every change. */
    using State = QcSessionState;

    QcClientSession(std::unique_ptr<QcTransport> transport, QString playerName, QObject *parent = nullptr);
    /*! Ticks on \a thread, where \a transport was made (a TLS connection, decisions E-560,
        E-872). */
    QcClientSession(std::unique_ptr<QcSessionThread> thread, std::unique_ptr<QcTransport> transport, QString playerName);
    ~QcClientSession() override;
    Q_DISABLE_COPY_MOVE(QcClientSession)

    /*! How the session ticks: on the tick thread, by a timer in this thread or by hand (tick()). */
    using TickMode = QcSessionTickMode;

    /*! Sends Hello and starts ticking on the tick thread, by a timer in this thread or by hand,
        as \a mode says. */
    void start(TickMode mode = TickMode::Threaded);
    /*! Sends Disconnect to the server and closes the connection; does nothing once disconnected. */
    void disconnectFromServer();

    /*! Returns the phase of the connection. */
    State state() const { return m_state; }
    /*! Returns the name this session plays under (sent in Hello). */
    QString playerName() const { return m_playerName; }
    /*! Returns why the session ended: the server's Disconnect text (translated, E-094), the
        connection error, or "Connection closed"; empty while connected. */
    QString disconnectReason() const { return m_disconnectReason; }
    /*! Returns why the connection ended; QcSessionEnding::None while connected. */
    QcSessionEnding ending() const { return m_ending; }
    /*! Returns the server's Disconnect message (empty if the link broke or the client left). */
    const QcMsg::Disconnect &disconnectMessage() const { return m_disconnectMessage; }
    /*! Sets the login data \a credentials for a network server (E-252), set before start();
        server name and MOTD after it. */
    void setCredentials(QcClientLogin::Credentials credentials) { m_login.setCredentials(std::move(credentials)); }
    /*! Returns the login data and what the server reported about itself. */
    const QcClientLogin &login() const { return m_login; }
    /*! Returns everyone on the server (QcMsg::PlayerList). */
    const QList<QcMsg::PlayerList::Entry> &players() const { return m_players; }
    /*! Returns the server's Welcome: world, seed, view distance streamed to us and our entity
        (all zero before it came). */
    const QcMsg::Welcome &welcome() const { return m_welcome; }
    /*! Returns the number of ticks the client ran since the start. */
    quint64 tickCount() const { return m_tickCount; }

    /*! Returns the world as the server sends it: content, blocks, chunk extras, world time,
        weather. */
    const QcWorldMirror &worldMirror() const { return m_worldMirror; }
    /*! Returns the blocks of the world mirror, null until the content arrived. */
    const QcClientWorld *world() const { return m_worldMirror.world(); }
    /*! Returns the content the server sent, null until it arrived. */
    std::shared_ptr<const QcContentRegistry> content() const { return m_worldMirror.content(); }

    /*! Sets the movement \a intent applied on every tick until changed. The sequence number is
        set by the session. Non-finite values are replaced, ranges clamped and yaw wrapped into
        [0, 360). */
    void setIntent(const QcPlayerInput &intent);
    /*! Returns the movement intent of the local player as last set with setIntent(). */
    const QcPlayerInput &intent() const { return m_prediction.intent(); }

    /*! Returns the game mode the server reported, its rules and the wish to fly (decisions
        E-282, E-284). */
    QcClientGameMode &gameMode() { return m_gameMode; }
    /*! Returns the game mode the server reported, its rules and the wish to fly, read-only. */
    const QcClientGameMode &gameMode() const { return m_gameMode; }

    /*! Returns the predicted player (motion one tick earlier, pending inputs, corrections, the
        minecart ridden). */
    const QcPrediction &prediction() const { return m_prediction; }
    /*! Returns the predicted state after the latest tick. */
    const QcEntityMotion &playerMotion() const { return m_prediction.motion(); }
    /*! Forgets the largest correction so far, here and in the tick core (E-257). */
    void resetCorrections();

    /*! Advances the client by one tick (TickMode::Manual only; otherwise a timer ticks). */
    void tick();
    /*! Returns the punctuality of the timer-driven ticks (decision E-560); empty with
        TickMode::Manual. Safe to read and reset while the tick thread runs. */
    QcTickTiming &tickTiming() const;
    /*! Returns the round trip of the inputs and bytes of the connection (health line, decision
        E-834). */
    QcLinkStats &linkStats() const;
    /*! Returns the round trip to the server, measured with probes by the tick thread (decision
        E-886); std::nullopt before the first answer. */
    std::optional<QcRoundTripSummary> latency() const;
    /*! Forgets the round trips so far, e.g. those of the busy start (tests); the probes go on. */
    void resetLatency();

    /*! Returns the hand: the block or entity the player looks at (updated every tick), mining
        progress. */
    const QcHandActions &hand() const { return m_hand; }
    /*! While \a mining, the targeted block is mined; progress runs from 0 to 1 and resets on target
        change. In creative mode a press breaks the aimed block at once (with the block
        prediction, E-853). */
    void setMining(bool mining);
    /*! Returns the block prediction (decision E-853): whether the blocks of the player's breaks
        and placements show before the server confirms them, and the number of the latest
        predicted action. */
    QcBlockPrediction &blockPrediction() { return m_blockPrediction; }
    /*! Returns the block prediction, read-only. */
    const QcBlockPrediction &blockPrediction() const { return m_blockPrediction; }
    /*! Uses the held item on the targeted block face (places blocks). Returns false without a
        target. */
    bool useItem();
    /*! The use button pressed: uses the item like useItem and, while held, again every
        QcHandActions::useRepeatTicks ticks until releaseUse or cancelUse (decision E-885).
        Returns what the first useItem() returns. */
    bool pressUse();
    /*! The use button released: a drawn launcher shoots (decision E-425); cancelUse lets it go
        without one. */
    void releaseUse() { m_hand.endUse(QcMsg::DrawLauncher::Action::Release, m_state == State::Playing); }
    /*! Lets a drawn launcher go without a shot when the use button is released (cancelled). */
    void cancelUse() { m_hand.endUse(QcMsg::DrawLauncher::Action::Cancel, m_state == State::Playing); }
    /*! Selects the hotbar \a slot and reports the change with inventoryChanged(). */
    void selectHotbarSlot(int slot);
    /*! Returns the inventory, equipment and crafting grid as the server sent them; moving items
        and crafting are requests to the server. */
    const QcClientInventory &inventoryMirror() const { return m_inventory; }
    /*! Returns the player's inventory as the server sent it. */
    const QcInventory &inventory() const { return m_inventory.inventory(); }
    /*! Returns the machine or chest screen the server opened (position, category, slots,
        progress). */
    const QcClientMachineScreen &machine() const { return m_machine; }
    /*! Closes the open machine screen here and asks the server to close it; nothing open, nothing
        sent. */
    void closeMachine();
    /*! Returns the trading screen of a villager the server opened, and trading with it (decision
        E-414). */
    QcClientTrading &trading() { return m_trading; }
    /*! Returns the trading screen the server opened, read-only. */
    const QcClientTrading &trading() const { return m_trading; }
    /*! Returns the editor of a sign the server opened (decision E-766). */
    QcClientSignEditor &signEditor() { return m_signEditor; }
    /*! Returns the editor of a sign the server opened, read-only. */
    const QcClientSignEditor &signEditor() const { return m_signEditor; }
    /*! Returns the stonecutter screen the server opened (decision E-785). */
    QcClientStonecutter &stonecutter() { return m_stonecutter; }
    /*! Returns the stonecutter screen the server opened, read-only. */
    const QcClientStonecutter &stonecutter() const { return m_stonecutter; }
    /*! Returns the enchanting table's screen the server opened (decision E-864). */
    QcClientEnchanting &enchanting() { return m_enchanting; }
    /*! Returns the enchanting table's screen the server opened, read-only. */
    const QcClientEnchanting &enchanting() const { return m_enchanting; }
    /*! Returns the anvil's screen the server opened (decision E-866). */
    QcClientAnvil &anvil() { return m_anvil; }
    /*! Returns the anvil's screen the server opened, read-only. */
    const QcClientAnvil &anvil() const { return m_anvil; }
    /*! Sends \a text as a chat line, trimmed; blank text sends nothing. */
    void sendChat(const QString &text);
    /*! Asks the server to pause (\a paused) while the pause menu is open, or to run on (decision
        E-877); the server only hears the local player of a world hosted in the game, and only
        while no one else is connected. */
    void requestPause(bool paused);
    /*! Returns whether the server's ticks pause (QcMsg::ServerPause): the player's own
        prediction rests too. */
    bool serverPaused() const { return m_serverPaused; }
    /*! Returns the server's ticks per normal tick as the world shows them
        (QcMsg::WorldTime::speed: more in a sprint, 0 under /tick freeze), 0 while it pauses even
        before its WorldTime arrives: the clock of the animated textures (QcAnimationClock,
        decision E-878). */
    double serverSpeed() const { return m_serverPaused ? 0.0 : worldMirror().timeSpeed(); }

    /*! Returns the mirrored entities by id, as the server last reported them. */
    const QHash<quint32, QcClientEntity> &entities() const { return m_entities.all(); }
    /*! Returns the mirror itself: the dying entities, its snapshot clock and buffering (decision
        E-271). */
    QcClientEntities &entityMirror() { return m_entities; }
    /*! Returns the entity mirror itself, read-only. */
    const QcClientEntities &entityMirror() const { return m_entities; }
    /*! Hits the targeted entity; returns false without one. */
    bool attack();
    /*! Asks the server to respawn the player; only while the player is dead. */
    void respawn();
    /*! Returns the view mode of the graphical client (also switchable through the control
        interface). */
    QcCameraMode cameraMode() const { return m_cameraMode; }
    /*! Sets the view \a mode of the graphical client. */
    void setCameraMode(QcCameraMode mode) { m_cameraMode = mode; }
    /*! Sets the \a language chat lines show content names in (a creature's kind, decision
        E-873). */
    void setContentLanguage(const QString &language) { m_contentLanguage = language; }
    /*! Sets the look \a appearance sent to the server; set before start() or at any time later. */
    void setAppearance(const QcAppearance &appearance);
    /*! Returns the look sent to the server. */
    const QcAppearance &appearance() const { return m_appearance; }

    /*! Returns the animation of the local player. \a alpha interpolates within the current tick. */
    QcEntityPose localPose(double alpha) const { return m_animation.localPose(alpha, m_prediction, m_hand.drawProgress(alpha)); }
    /*! Returns the tilt of the view after a hit \a alpha into the current tick (decision E-971). */
    QcHurtTilt::Tilt hurtTilt(double alpha) const { return m_animation.hurtTilt(alpha); }
    /*! Returns the timers of the hand animation (swing, use) at \a alpha within the current tick.
     */
    QcHandTimers handTimers(double alpha) const { return m_animation.handTimers(alpha); }

    /*! Throws one item (or the whole stack with \a wholeStack) of the selected hotbar slot;
        nothing when there is none. Returns whether an item was thrown. */
    bool dropItem(bool wholeStack);
    /*! Returns health, food and experience as the server last reported them. */
    const QcClientVitals &vitals() const { return m_vitals; }
    /*! Returns whether the local player is dead (vitals()). */
    bool isDead() const { return m_vitals.isDead(); }
    /*! Returns the progress in the adventure guide as the server last reported it (decision
        E-923). */
    const QcClientAdvancements &advancements() const { return m_advancements; }

    /*! Returns the server's debug data: what is asked for and what arrived
        (QcClientDebugSubscription). */
    QcClientDebugSubscription &debug() { return m_debug; }
    /*! Returns the server's debug data, read-only. */
    const QcClientDebugSubscription &debug() const { return m_debug; }

    /*! Returns the admin channel (decision E-848): what an operator follows and asks the server
        for. */
    QcClientAdmin &admin() { return m_admin; }
    /*! Returns the admin channel, read-only. */
    const QcClientAdmin &admin() const { return m_admin; }

    /*! Returns the recent sounds (server and own) and the ambience levels around the player,
        faded towards the surroundings every tick. */
    const QcClientSounds &sounds() const { return m_sounds; }
    /*! Forgets the recent sounds (control interface). */
    void clearRecentSounds() { m_sounds.clearRecent(); }

signals:
    /*! The phase of the connection changed to \a state. */
    void stateChanged(QcSessionStates::State state);
    /*! The server's content arrived; world() and content() are set. */
    void contentReady();
    /*! The chunk at \a pos arrived from the server. */
    void chunkLoaded(QcChunkPos pos);
    /*! The server unloaded the chunk at \a pos. */
    void chunkUnloaded(QcChunkPos pos);
    /*! The server changed the block at \a pos; \a previous is the block before. */
    void blockChanged(QcBlockPos pos, QcBlockId previous);
    /*! Emitted when the block at \a pos changed from \a previous because the local player's
        prediction set it, or the rollback of a refused one (decision E-853); meshed before the
        others. */
    void playerBlockChanged(QcBlockPos pos, QcBlockId previous);
    /*! The client ran a tick (also while the player is dead). */
    void ticked();
    /*! The session ended; \a reason says why (disconnectReason()). */
    void disconnected(const QString &reason);
    /*! The inventory, equipment or the selected hotbar slot changed. */
    void inventoryChanged();
    /*! The player's health changed (vitals()). */
    void healthChanged();
    /*! The player's food changed (vitals()). */
    void foodChanged();
    /*! The player's experience changed (vitals()). */
    void experienceChanged();
    /*! The player's air under water changed (vitals()). */
    void airChanged();
    /*! The progress in the adventure guide changed (advancements()). */
    void advancementsChanged();
    /*! The server announced that the local player reached \a advancement just now (the toast). */
    void advancementReached(const QString &advancement);
    /*! The server opened a machine or chest screen (machine()). */
    void machineOpened();
    /*! Contents or progress of the open machine changed. */
    void machineChanged();
    /*! The machine screen closed, by the server or by closeMachine(). */
    void machineClosed();
    /*! Emitted when the server opened a crafting table's grid of \a size x \a size slots. */
    void craftingOpened(int size);
    /*! A chat line arrived; \a text is ready to show (content names in the content language). */
    void chatReceived(const QString &text);
    /*! The list of players on the server changed (players()). */
    void playersChanged();
    /*! The server reported a game mode (gameMode()). */
    void gameModeChanged();
    /*! The server's ticks paused or resumed (serverPaused()). */
    void serverPauseChanged();
    /*! The mode refused a mining or use of the player (adventure allow-lists); at most once
        every QcRefusalThrottle::reportTicks. */
    void actionRefused();
    /*! Emitted with a sound \a cue to play now. */
    void soundCue(const QcSoundCue &cue);

private:
    // Gives the parts that talk to the server their way out (both constructors).
    void connectParts();
    void handleMessage(const QcMessage &message);
    // Message groups; each returns false for messages outside its group.
    bool handleConnectionMessage(const QcMessage &message);
    // The world mirror's messages (QcWorldMirror): reports what they changed.
    bool handleWorldMessage(const QcMessage &message);
    bool handlePlayerMessage(const QcMessage &message);
    bool handleEntityMessage(const QcMessage &message);
    // The machine screen's messages (QcClientMachineScreen).
    bool handleMachineMessage(const QcMessage &message);
    void onWelcome(const QcMsg::Welcome &welcome);
    // Health, food or experience changed (QcClientVitals::apply).
    void onVitalsChanged(QcClientVitals::Change change);
    // Closes the connection; `reason` is reported as the disconnect reason, `ending` as ending().
    void closeWithError(const QString &reason, QcSessionEnding ending);
    void setEnding(QcSessionEnding ending);
    void setState(State state);
    // One event of the tick core, in the core's order.
    void onEvent(const QcSessionEvent &event);
    // A tick of the core: the parts tick, the player as predicted; on foot (or flying) the stride,
    // landing and step sounds, riding the seat of the cart.
    void onTicked(const QcSessionEvent::Ticked &event);
    // The core took the server's state of the player.
    void onReconciled(const QcSessionEvent::Reconciled &reconciled);
    // Takes the core's prediction and wish to fly (QcPrediction::mirror).
    void adopt(const QcSessionEvent::Prediction &prediction);
    void onClosed(const QcSessionEvent::Closed &closed);
    // Aims the hand again (no target before the world arrived).
    void refreshTarget();
    // Applies what a hand action did to the arm and the screens; false when it did nothing.
    bool apply(QcHandActions::HandEffect effect);
    // The splash and paddle strokes of the boat the player drives.
    void rowingSounds();
    void cue(QcSoundCue cue);
    // Sends a request of a part (inventory, hand) over the connection.
    void sendToServer(const QcMessage &message);
    // Shows the blocks a hand action is expected to set, here and in the tick core, and returns the
    // action's number; 0 when nothing is predicted (QcHandActions::Predictor).
    quint32 predictBlocks(const QList<QcMsg::BlockUpdate> &blocks);
    void updateSounds();

    QString m_playerName;
    State m_state = State::Connecting;
    QString m_disconnectReason;
    QcSessionEnding m_ending = QcSessionEnding::None;
    QString m_contentLanguage = QStringLiteral("en");
    QcMsg::Disconnect m_disconnectMessage;
    QcClientLogin m_login;
    QList<QcMsg::PlayerList::Entry> m_players;
    bool m_serverPaused = false;
    // The server's Welcome: world, seed, view distance and our entity (all zero before it came).
    QcMsg::Welcome m_welcome;
    QcWorldMirror m_worldMirror;
    quint64 m_tickCount = 0;
    // When a refusal is reported (actionRefused).
    QcRefusalThrottle m_refusals;

    // Parts in dependency order: the hand reads the parts declared before it; what its actions mean
    // for the animation, the machine screen and the trading the session applies (apply()).
    QcPrediction m_prediction;
    QcClientInventory m_inventory{[this](const QcMessage &message) { sendToServer(message); }};
    QcClientEntities m_entities;
    QcClientGameMode m_gameMode;
    QcLocalAnimation m_animation;
    QcClientMachineScreen m_machine;
    QcClientTrading m_trading;
    QcClientSignEditor m_signEditor;
    QcClientStonecutter m_stonecutter;
    QcClientEnchanting m_enchanting;
    QcClientAnvil m_anvil;
    QcHandActions m_hand{{m_prediction, m_inventory, m_entities},
                         m_gameMode,
                         [this](const QcMessage &message) { sendToServer(message); },
                         [this](const QcSoundCue &sound) { cue(sound); },
                         [this](const QList<QcMsg::BlockUpdate> &blocks) { return predictBlocks(blocks); }};
    QcBlockPrediction m_blockPrediction;

    QcAppearance m_appearance;
    bool m_helloSent = false;
    bool m_appearancePending = false;
    QcCameraMode m_cameraMode = QcCameraMode::FirstPerson;
    QcClientVitals m_vitals;
    QcClientAdvancements m_advancements;
    QcClientDebugSubscription m_debug;
    QcClientAdmin m_admin;
    QcClientSounds m_sounds;
    // Calls back into the members above; the destructor stops it first.
    std::unique_ptr<QcSessionLink> m_link;
};
