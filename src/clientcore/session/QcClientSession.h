#pragma once

#include "clientcore/camera/QcViewCamera.h"
#include "clientcore/meshing/QcEntityPose.h"
#include "clientcore/player/QcClientGameMode.h"
#include "clientcore/player/QcClientInventory.h"
#include "clientcore/player/QcClientRiding.h"
#include "clientcore/player/QcClientVitals.h"
#include "clientcore/player/QcHandActions.h"
#include "clientcore/player/QcHandTimers.h"
#include "clientcore/player/QcLocalAnimation.h"
#include "clientcore/player/QcPrediction.h"
#include "clientcore/session/QcChunkExtras.h"
#include "clientcore/session/QcClientDebugSubscription.h"
#include "clientcore/session/QcClientEntities.h"
#include "clientcore/session/QcClientLogin.h"
#include "clientcore/session/QcClientMachineScreen.h"
#include "clientcore/session/QcClientSounds.h"
#include "clientcore/session/QcClientTrading.h"
#include "clientcore/session/QcClientWorld.h"
#include "clientcore/weather/QcClientWeather.h"
#include "common/gameplay/QcInventory.h"
#include "common/physics/QcEntityMotion.h"
#include "common/physics/QcPlayerInput.h"
#include "common/physics/QcRaycast.h"
#include "common/protocol/QcMessages.h"

#include <QHash>
#include <QList>
#include <QObject>

#include <memory>
#include <optional>

class QcConnection;
class QcTransport;
class QTimer;

// Client side of one connection: mirrors the world the server sends and distributes its messages,
// drives the local player's parts (clientcore/player: prediction, hand, animation, inventory) once
// per tick and sends their requests. GUI-free; the renderer, the UI and the control interface all
// observe a session. It stays their facade: the getters below forward to the parts (decision E-530).
class QcClientSession : public QObject
{
    Q_OBJECT

public:
    enum class State { Connecting, Loading, Playing, Disconnected };
    Q_ENUM(State)

    QcClientSession(std::unique_ptr<QcTransport> transport, QString playerName, QObject *parent = nullptr);
    ~QcClientSession() override;

    enum class TickMode { Timer, Manual };

    // Sends Hello. TickMode::Timer drives tick() at 20 Hz; Manual leaves it to the caller (tests).
    void start(TickMode mode = TickMode::Timer);
    void disconnectFromServer();

    State state() const { return m_state; }
    // Why the session ended (translated, E-094) and the server's Disconnect (empty if the link broke).
    QString disconnectReason() const { return m_disconnectReason; }
    const QcMsg::Disconnect &disconnectMessage() const { return m_disconnectMessage; }
    // Login data for a network server (E-252), set before start(); server name and MOTD after it.
    void setCredentials(QcClientLogin::Credentials credentials) { m_login.setCredentials(std::move(credentials)); }
    const QcClientLogin &login() const { return m_login; }
    // Everyone on the server (QcMsg::PlayerList) and the view distance streamed to us (Welcome).
    const QList<QcMsg::PlayerList::Entry> &players() const { return m_players; }
    int viewDistance() const { return m_viewDistance; }
    QString worldName() const { return m_worldName; }
    qint64 seed() const { return m_seed; }
    quint32 entityId() const { return m_entityId; }
    quint64 tickCount() const { return m_tickCount; }

    // Null until the content arrived.
    const QcClientWorld *world() const { return m_world.get(); }
    std::shared_ptr<const QcContentRegistry> content() const { return m_content; }

    // Movement intent applied on every tick until changed. The sequence number is set by the session.
    // Non-finite values are replaced, ranges clamped and yaw wrapped into [0, 360).
    void setIntent(const QcPlayerInput &intent);
    const QcPlayerInput &intent() const { return m_prediction.intent(); }

    // The game mode the server reported, its rules and the wish to fly (decisions E-282, E-284).
    QcClientGameMode &gameMode() { return m_gameMode; }
    const QcClientGameMode &gameMode() const { return m_gameMode; }

    // Predicted state after the latest tick and the state one tick earlier, for interpolation.
    const QcEntityMotion &playerMotion() const { return m_prediction.motion(); }
    const QcEntityMotion &previousPlayerMotion() const { return m_prediction.previousMotion(); }
    qsizetype pendingInputCount() const { return m_prediction.pendingInputCount(); }
    // Metres the server moved the predicted player: latest reconcile, most since reset (E-257).
    double lastCorrection() const { return m_prediction.lastCorrection(); }
    double largestCorrection() const { return m_prediction.largestCorrection(); }
    void resetCorrections() { m_prediction.resetCorrections(); }
    bool hasServerState() const { return m_prediction.hasServerState(); }
    // The minecart the player rides (decision E-382): no prediction then, the player sits in the cart.
    const QcClientRiding &riding() const { return m_prediction.riding(); }

    // Advances the client by one tick; normally driven by the internal timer.
    void tick();

    // Block the player looks at (within reach), updated every tick.
    const std::optional<QcRaycastHit> &target() const { return m_hand.target(); }
    // While true, the targeted block is mined; progress runs from 0 to 1 and resets on target change.
    void setMining(bool mining) { m_hand.setMining(mining); }
    double miningProgress() const { return m_hand.miningProgress(); }
    // Uses the held item on the targeted block face (places blocks). Returns false without a target.
    bool useItem();
    // The use button released: a drawn launcher shoots (decision E-425); cancelUse lets it go without one.
    void releaseUse() { m_hand.endUse(QcMsg::DrawLauncher::Action::Release, m_state == State::Playing); }
    void cancelUse() { m_hand.endUse(QcMsg::DrawLauncher::Action::Cancel, m_state == State::Playing); }
    void selectHotbarSlot(int slot);
    // Slots 0..35 inventory, 36..44 crafting grid (see QcMsg::MoveInventoryItem); count 0 = all.
    void moveInventoryItem(int from, int to, int count = 0) { m_inventory.moveItem(from, to, count); }
    // Shift-click: the server moves the stack to where it belongs; the crafting result
    // (QcMsg::QuickMoveItem::craftingResultSlot) is crafted as often as possible.
    void quickMoveItem(int slot) { m_inventory.quickMoveItem(slot); }
    void takeCraftingResult() { m_inventory.takeCraftingResult(); }
    void craftRecipe(const QString &recipe, int count = 1) { m_inventory.craftRecipe(recipe, count); }
    void closeCrafting() { m_inventory.closeCrafting(); }
    // The machine or chest screen the server opened (position, category, slots, progress).
    const QcClientMachineScreen &machine() const { return m_machine; }
    void closeMachine();
    // The trading screen of a villager the server opened, and trading with it (decision E-414).
    QcClientTrading &trading() { return m_trading; }
    const QcClientTrading &trading() const { return m_trading; }
    const QList<QcItemStack> &craftingGrid() const { return m_inventory.craftingGrid(); }
    int craftingSize() const { return m_inventory.craftingSize(); }
    QcItemStack craftingResult() const { return m_inventory.craftingResult(); }
    void sendChat(const QString &text);
    const QcInventory &inventory() const { return m_inventory.inventory(); }
    const QcEquipment &equipment() const { return m_inventory.equipment(); }

    const QHash<quint32, QcClientEntity> &entities() const { return m_entities.all(); }
    // The mirror itself: its snapshot clock and buffering (decision E-271).
    QcClientEntities &entityMirror() { return m_entities; }
    // Entity under the crosshair when it is closer than any targeted block.
    [[nodiscard]] std::optional<quint32> targetEntity() const { return m_hand.targetEntity(); }
    // Hits the targeted entity; returns false without one.
    bool attack();
    void respawn();
    // View mode of the graphical client (also switchable through the control interface).
    QcCameraMode cameraMode() const { return m_cameraMode; }
    void setCameraMode(QcCameraMode mode) { m_cameraMode = mode; }
    // The look sent to the server; set before start() or at any time later.
    void setAppearance(const QcAppearance &appearance);
    const QcAppearance &appearance() const { return m_appearance; }
    // Entities that died and play their death animation for a moment; not targetable.
    const QHash<quint32, QcClientEntity> &dyingEntities() const { return m_entities.dying(); }

    // Animation of the local player. `alpha` interpolates within the current tick.
    QcEntityPose localPose(double alpha) const { return m_animation.localPose(alpha, m_prediction, m_hand.drawProgress(alpha)); }
    QcHandTimers handTimers(double alpha) const { return m_animation.handTimers(alpha); }
    // Pose of a mirrored entity from its server state and client timers.
    static QcEntityPose poseOf(const QcClientEntity &entity, double alpha) { return QcLocalAnimation::poseOf(entity, alpha); }

    // Animation lengths in ticks; those of the hand are QcHandTimers'.
    static constexpr int swingTicks = QcLocalAnimation::swingTicks;
    static constexpr int deathTicks = QcLocalAnimation::deathTicks;
    // Throws one item (or the whole stack) of the selected hotbar slot; false when there is none.
    bool dropItem(bool wholeStack);
    // Health, food and experience as the server last reported them.
    const QcClientVitals &vitals() const { return m_vitals; }
    bool isDead() const { return m_vitals.isDead(); }
    // Seconds of world time, advanced locally between server updates while the time of day runs, by
    // timeSpeed() per tick (the server's tick rate or sprint, WorldTime::speed).
    double worldTime() const { return m_worldTime.time; }
    double timeSpeed() const { return m_worldTime.speed; }
    // The weather as shown: strengths, climate, lightning flashes (decision E-395).
    const QcClientWeather &weather() const { return m_weather; }
    // Labels (E-222), spinning spawner creatures (E-520) and a number that changes with the labels.
    const QHash<QcChunkPos, QList<QcWorldLabel>> &labels() const { return m_extras.labels(); }
    quint64 labelGeneration() const { return m_extras.labelGeneration(); }
    const QHash<QcBlockPos, int> &spawnerDisplays() const { return m_extras.spawners(); }

    // The server's debug data: what is asked for and what arrived (QcClientDebugSubscription).
    QcClientDebugSubscription &debug() { return m_debug; }
    const QcClientDebugSubscription &debug() const { return m_debug; }

    // Recent sounds (server and own), oldest first, at most QcClientSounds::recentLimit; cleared by
    // clearRecentSounds() (control interface).
    const QList<QcSoundCue> &recentSounds() const { return m_sounds.recent(); }
    void clearRecentSounds() { m_sounds.clearRecent(); }
    // Ambience loop levels around the player, faded towards the surroundings every tick.
    const QcAmbience::Levels &ambience() const { return m_sounds.ambience(); }
    // Mining strikes make a sound every this many ticks.
    static constexpr int miningHitTicks = QcHandActions::miningHitTicks;
    // Melee reach of the player.
    static constexpr double attackReach = QcHandActions::attackReach;

signals:
    void stateChanged(QcClientSession::State state);
    void contentReady();
    void chunkLoaded(QcChunkPos pos);
    void chunkUnloaded(QcChunkPos pos);
    void blockChanged(QcBlockPos pos, QcBlockId previous);
    void ticked();
    void disconnected(const QString &reason);
    void inventoryChanged();
    void healthChanged();
    void foodChanged();
    void experienceChanged();
    void machineOpened();
    // Contents or progress of the open machine changed.
    void machineChanged();
    void machineClosed();
    // The server opened a crafting table's grid.
    void craftingOpened(int size);
    void chatReceived(const QString &text);
    void playersChanged();
    void gameModeChanged();
    // A sound to play now (see QcSoundCue).
    void soundCue(const QcSoundCue &cue);

private:
    void handleMessage(QcMessage &&message);
    // Message groups; each returns false for messages outside its group.
    bool handleConnectionMessage(const QcMessage &message);
    bool handleWorldMessage(const QcMessage &message);
    bool handlePlayerMessage(const QcMessage &message);
    bool handleEntityMessage(const QcMessage &message);
    // The machine screen's messages (QcClientMachineScreen).
    bool handleMachineMessage(const QcMessage &message);
    void onWelcome(const QcMsg::Welcome &welcome);
    void onContentSync(const QcMsg::ContentSync &sync);
    void onChunkData(const QcMsg::ChunkData &chunkData);
    // Health, food or experience changed (QcClientVitals::apply).
    void onVitalsChanged(QcClientVitals::Change change);
    // Closes the connection; `reason` is reported as the disconnect reason.
    void closeWithError(const QString &reason);
    void setState(State state);
    // The prediction took the server's state of the player.
    void reconcile(const QcMsg::PlayerState &state);
    // Predicts the player by one tick (QcPrediction) and sends the input; on foot (or flying) with
    // the game mode's wish to fly, the stride, landing and step sounds.
    void predict();
    void onClosed();
    // Aims the hand again (no target before the world arrived).
    void refreshTarget();
    // Applies what a hand action did to the arm and the screens; false when it did nothing.
    bool apply(QcHandActions::HandEffect effect);
    void cue(QcSoundCue cue);
    // Sends a request of a part (inventory, hand) over the connection.
    void sendToServer(const QcMessage &message);
    void updateSounds();

    std::unique_ptr<QcConnection> m_connection;
    QString m_playerName;
    State m_state = State::Connecting;
    QString m_disconnectReason;
    QcMsg::Disconnect m_disconnectMessage;
    QcClientLogin m_login;
    QList<QcMsg::PlayerList::Entry> m_players;
    int m_viewDistance = 0;
    QString m_worldName;
    qint64 m_seed = 0;
    quint32 m_entityId = 0;
    std::shared_ptr<const QcContentRegistry> m_content;
    std::unique_ptr<QcClientWorld> m_world;
    QTimer *m_tickTimer = nullptr;
    quint64 m_tickCount = 0;

    // Parts in dependency order: the hand reads the parts declared before it; what its actions mean
    // for the animation, the machine screen and the trading the session applies (apply()).
    QcPrediction m_prediction;
    QcClientInventory m_inventory{[this](const QcMessage &message) { sendToServer(message); }};
    QcClientEntities m_entities;
    QcClientGameMode m_gameMode;
    QcLocalAnimation m_animation;
    QcClientMachineScreen m_machine;
    QcClientTrading m_trading;
    QcHandActions m_hand{{m_prediction, m_inventory, m_entities},
                         m_gameMode,
                         [this](const QcMessage &message) { sendToServer(message); },
                         [this](const QcSoundCue &sound) { cue(sound); }};

    QcAppearance m_appearance;
    bool m_helloSent = false;
    bool m_appearancePending = false;
    QcCameraMode m_cameraMode = QcCameraMode::FirstPerson;
    QcClientVitals m_vitals;
    // The latest world time from the server.
    QcMsg::WorldTime m_worldTime{0.0, 600.0, true, 1.0};
    QcChunkExtras m_extras;
    QcClientDebugSubscription m_debug;
    QcClientSounds m_sounds;
    QcClientWeather m_weather;
};
