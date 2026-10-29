#pragma once

#include "clientcore/admin/QcClientAdmin.h"
#include "clientcore/camera/QcViewCamera.h"
#include "clientcore/meshing/QcEntityPose.h"
#include "clientcore/player/QcClientAdvancements.h"
#include "clientcore/player/QcClientGameMode.h"
#include "clientcore/player/QcClientInventory.h"
#include "clientcore/player/QcClientVitals.h"
#include "clientcore/player/QcHandActions.h"
#include "clientcore/player/QcHandTimers.h"
#include "clientcore/player/QcLocalAnimation.h"
#include "clientcore/player/QcPrediction.h"
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

// Client side of one connection: distributes the server's messages to its parts (the world mirror
// QcWorldMirror, the entity mirror, the screens), drives the local player's parts (clientcore/player:
// prediction, hand, animation, inventory) once per tick and sends their requests. GUI-free; the
// renderer, the UI and the control interface all observe a session. Its getters hand out the parts
// themselves; the session keeps what combines them, the player's actions and the signals (decisions
// E-530, E-717).
// Connection, ticks and prediction run in a tick core (QcSessionLink, E-560), timer-driven on a thread
// of its own; the session applies the core's ordered events in its own thread, between two frames.
class QcClientSession : public QObject
{
    Q_OBJECT

public:
    enum class State { Connecting, Loading, Playing, Disconnected };
    Q_ENUM(State)

    QcClientSession(std::unique_ptr<QcTransport> transport, QString playerName, QObject *parent = nullptr);
    // Ticks on `thread`, where `transport` was made (a TLS connection, decisions E-560, E-872).
    QcClientSession(std::unique_ptr<QcSessionThread> thread, std::unique_ptr<QcTransport> transport, QString playerName);
    ~QcClientSession() override;
    Q_DISABLE_COPY_MOVE(QcClientSession)

    using TickMode = QcSessionTickMode;

    // A refused action (adventure mode, decision E-735) is reported at most once in this many ticks.
    static constexpr quint64 refusalReportTicks = 40;

    // Sends Hello and starts ticking on the tick thread, by a timer in this thread or by hand.
    void start(TickMode mode = TickMode::Threaded);
    void disconnectFromServer();

    State state() const { return m_state; }
    // The name this session plays under (sent in Hello).
    QString playerName() const { return m_playerName; }
    // Why the session ended (translated, E-094) and the server's Disconnect (empty if the link broke).
    QString disconnectReason() const { return m_disconnectReason; }
    const QcMsg::Disconnect &disconnectMessage() const { return m_disconnectMessage; }
    // Login data for a network server (E-252), set before start(); server name and MOTD after it.
    void setCredentials(QcClientLogin::Credentials credentials) { m_login.setCredentials(std::move(credentials)); }
    const QcClientLogin &login() const { return m_login; }
    // Everyone on the server (QcMsg::PlayerList).
    const QList<QcMsg::PlayerList::Entry> &players() const { return m_players; }
    // The server's Welcome: world, seed, view distance streamed to us and our entity (all zero before it came).
    const QcMsg::Welcome &welcome() const { return m_welcome; }
    quint64 tickCount() const { return m_tickCount; }

    // The world as the server sends it: content, blocks, chunk extras, world time, weather.
    const QcWorldMirror &worldMirror() const { return m_worldMirror; }
    // Its blocks, null until the content arrived.
    const QcClientWorld *world() const { return m_worldMirror.world(); }
    std::shared_ptr<const QcContentRegistry> content() const { return m_worldMirror.content(); }

    // Movement intent applied on every tick until changed. The sequence number is set by the session.
    // Non-finite values are replaced, ranges clamped and yaw wrapped into [0, 360).
    void setIntent(const QcPlayerInput &intent);
    const QcPlayerInput &intent() const { return m_prediction.intent(); }

    // The game mode the server reported, its rules and the wish to fly (decisions E-282, E-284).
    QcClientGameMode &gameMode() { return m_gameMode; }
    const QcClientGameMode &gameMode() const { return m_gameMode; }

    // The predicted player (motion one tick earlier, pending inputs, corrections, the minecart ridden).
    const QcPrediction &prediction() const { return m_prediction; }
    // Predicted state after the latest tick.
    const QcEntityMotion &playerMotion() const { return m_prediction.motion(); }
    // Forgets the largest correction so far, here and in the tick core (E-257).
    void resetCorrections();

    // Advances the client by one tick (TickMode::Manual only; otherwise a timer ticks).
    void tick();
    // Punctuality of the timer-driven ticks (decision E-560); empty with TickMode::Manual. Safe to
    // read and reset while the tick thread runs.
    QcTickTiming &tickTiming() const;
    // Round trip of the inputs and bytes of the connection (health line, decision E-834).
    QcLinkStats &linkStats() const;
    // The round trip to the server, measured with probes by the tick thread (decision E-886);
    // std::nullopt before the first answer.
    std::optional<QcRoundTripSummary> latency() const;
    // Forgets the round trips so far, e.g. those of the busy start (tests); the probes go on.
    void resetLatency();

    // The hand: the block or entity the player looks at (updated every tick), mining progress.
    const QcHandActions &hand() const { return m_hand; }
    // While true, the targeted block is mined; progress runs from 0 to 1 and resets on target change.
    // In creative mode a press breaks the aimed block at once (with the block prediction, E-853).
    void setMining(bool mining);
    // Whether the session shows the blocks of the player's breaks and placements before the server
    // confirms them (decision E-853); on unless QTCRAFT_BLOCK_PREDICTION=0. Applies from the next
    // action on.
    static bool blockPredictionWanted();
    void setBlockPrediction(bool on) { m_blockPrediction = on; }
    bool blockPrediction() const { return m_blockPrediction; }
    // The number of the latest predicted action, 0 before the first; numbers start anew with every
    // session (a reconnect too).
    quint32 blockSequence() const { return m_blockSequence; }
    // Uses the held item on the targeted block face (places blocks). Returns false without a target.
    bool useItem();
    // The use button pressed: uses the item like useItem and, while held, again every
    // QcHandActions::useRepeatTicks ticks until releaseUse or cancelUse (decision E-885).
    bool pressUse();
    // The use button released: a drawn launcher shoots (decision E-425); cancelUse lets it go without one.
    void releaseUse() { m_hand.endUse(QcMsg::DrawLauncher::Action::Release, m_state == State::Playing); }
    void cancelUse() { m_hand.endUse(QcMsg::DrawLauncher::Action::Cancel, m_state == State::Playing); }
    void selectHotbarSlot(int slot);
    // The inventory, equipment and crafting grid as the server sent them; moving items and crafting
    // are requests to the server.
    const QcClientInventory &inventoryMirror() const { return m_inventory; }
    const QcInventory &inventory() const { return m_inventory.inventory(); }
    // The machine or chest screen the server opened (position, category, slots, progress).
    const QcClientMachineScreen &machine() const { return m_machine; }
    void closeMachine();
    // The trading screen of a villager the server opened, and trading with it (decision E-414).
    QcClientTrading &trading() { return m_trading; }
    const QcClientTrading &trading() const { return m_trading; }
    // The editor of a sign the server opened (decision E-766).
    QcClientSignEditor &signEditor() { return m_signEditor; }
    const QcClientSignEditor &signEditor() const { return m_signEditor; }
    // The stonecutter screen the server opened (decision E-785).
    QcClientStonecutter &stonecutter() { return m_stonecutter; }
    const QcClientStonecutter &stonecutter() const { return m_stonecutter; }
    // The enchanting table's screen the server opened (decision E-864).
    QcClientEnchanting &enchanting() { return m_enchanting; }
    const QcClientEnchanting &enchanting() const { return m_enchanting; }
    // The anvil's screen the server opened (decision E-866).
    QcClientAnvil &anvil() { return m_anvil; }
    const QcClientAnvil &anvil() const { return m_anvil; }
    void sendChat(const QString &text);
    // Asks the server to pause while the pause menu is open (decision E-877); the server only hears
    // the local player of a world hosted in the game, and only while no one else is connected.
    void requestPause(bool paused);
    // Whether the server's ticks pause (QcMsg::ServerPause): the player's own prediction rests too.
    bool serverPaused() const { return m_serverPaused; }
    // The server's ticks per normal tick as the world shows them (QcMsg::WorldTime::speed: more in a
    // sprint, 0 under /tick freeze), 0 while it pauses even before its WorldTime arrives: the clock
    // of the animated textures (QcAnimationClock, decision E-878).
    double serverSpeed() const { return m_serverPaused ? 0.0 : worldMirror().timeSpeed(); }

    const QHash<quint32, QcClientEntity> &entities() const { return m_entities.all(); }
    // The mirror itself: the dying entities, its snapshot clock and buffering (decision E-271).
    QcClientEntities &entityMirror() { return m_entities; }
    const QcClientEntities &entityMirror() const { return m_entities; }
    // Hits the targeted entity; returns false without one.
    bool attack();
    void respawn();
    // View mode of the graphical client (also switchable through the control interface).
    QcCameraMode cameraMode() const { return m_cameraMode; }
    void setCameraMode(QcCameraMode mode) { m_cameraMode = mode; }
    // The language chat lines show content names in (a creature's kind, decision E-873).
    void setContentLanguage(const QString &language) { m_contentLanguage = language; }
    // The look sent to the server; set before start() or at any time later.
    void setAppearance(const QcAppearance &appearance);
    const QcAppearance &appearance() const { return m_appearance; }

    // Animation of the local player. `alpha` interpolates within the current tick.
    QcEntityPose localPose(double alpha) const { return m_animation.localPose(alpha, m_prediction, m_hand.drawProgress(alpha)); }
    QcHandTimers handTimers(double alpha) const { return m_animation.handTimers(alpha); }

    // Throws one item (or the whole stack) of the selected hotbar slot; false when there is none.
    bool dropItem(bool wholeStack);
    // Health, food and experience as the server last reported them.
    const QcClientVitals &vitals() const { return m_vitals; }
    bool isDead() const { return m_vitals.isDead(); }
    // The progress in the adventure guide as the server last reported it (decision E-923).
    const QcClientAdvancements &advancements() const { return m_advancements; }

    // The server's debug data: what is asked for and what arrived (QcClientDebugSubscription).
    QcClientDebugSubscription &debug() { return m_debug; }
    const QcClientDebugSubscription &debug() const { return m_debug; }

    // The admin channel (decision E-848): what an operator follows and asks the server for.
    QcClientAdmin &admin() { return m_admin; }
    const QcClientAdmin &admin() const { return m_admin; }

    // Recent sounds (server and own) and the ambience levels around the player, faded towards the
    // surroundings every tick.
    const QcClientSounds &sounds() const { return m_sounds; }
    // Forgets the recent sounds (control interface).
    void clearRecentSounds() { m_sounds.clearRecent(); }

signals:
    void stateChanged(QcClientSession::State state);
    void contentReady();
    void chunkLoaded(QcChunkPos pos);
    void chunkUnloaded(QcChunkPos pos);
    void blockChanged(QcBlockPos pos, QcBlockId previous);
    // A block the local player's prediction set, or the rollback of a refused one (decision E-853);
    // meshed before the others.
    void playerBlockChanged(QcBlockPos pos, QcBlockId previous);
    void ticked();
    void disconnected(const QString &reason);
    void inventoryChanged();
    void healthChanged();
    void foodChanged();
    void experienceChanged();
    void advancementsChanged();
    // The server announced that the local player reached `advancement` just now (the toast).
    void advancementReached(const QString &advancement);
    void machineOpened();
    // Contents or progress of the open machine changed.
    void machineChanged();
    void machineClosed();
    // The server opened a crafting table's grid.
    void craftingOpened(int size);
    void chatReceived(const QString &text);
    void playersChanged();
    void gameModeChanged();
    void serverPauseChanged();
    // The mode refused a mining or use of the player (adventure allow-lists); at most once every
    // refusalReportTicks.
    void actionRefused();
    // A sound to play now (see QcSoundCue).
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
    // Closes the connection; `reason` is reported as the disconnect reason.
    void closeWithError(const QString &reason);
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
    void onClosed(const QString &error);
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
    QString m_contentLanguage = QStringLiteral("en");
    QcMsg::Disconnect m_disconnectMessage;
    QcClientLogin m_login;
    QList<QcMsg::PlayerList::Entry> m_players;
    bool m_serverPaused = false;
    // The server's Welcome: world, seed, view distance and our entity (all zero before it came).
    QcMsg::Welcome m_welcome;
    QcWorldMirror m_worldMirror;
    quint64 m_tickCount = 0;
    // The first tick at which the next refusal is reported (actionRefused).
    quint64 m_nextRefusalReport = 0;

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
    bool m_blockPrediction = blockPredictionWanted();
    quint32 m_blockSequence = 0;

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
