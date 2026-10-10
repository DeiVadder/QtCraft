#pragma once

#include "QcClientEntities.h"
#include "QcAmbience.h"
#include "QcClientWorld.h"
#include "QcEntityMotion.h"
#include "QcEntityPose.h"
#include "QcViewCamera.h"
#include "QcInventory.h"
#include "QcLocalSounds.h"
#include "QcMessages.h"
#include "QcPlayerInput.h"
#include "QcRaycast.h"
#include "QcSoundCue.h"

#include <QHash>
#include <QList>
#include <QObject>

#include <memory>
#include <optional>

class QcConnection;
class QcTransport;
class QTimer;

// Client side of one connection: mirrors the world the server sends, predicts the local player
// with the shared physics and reconciles with authoritative server state. GUI-free; the renderer,
// the UI and the control interface all observe a session.
class QcClientSession : public QObject
{
    Q_OBJECT

public:
    enum class State { Connecting, Loading, Playing, Disconnected };
    Q_ENUM(State)

    QcClientSession(std::unique_ptr<QcTransport> transport, QString playerName, QObject *parent = nullptr);
    ~QcClientSession() override;

    enum class TickMode { Timer, Manual };

    // Sends Hello. With TickMode::Timer a 20 Hz timer drives tick(); Manual leaves it to the caller
    // (deterministic tests).
    void start(TickMode mode = TickMode::Timer);
    void disconnectFromServer();

    State state() const { return m_state; }
    QString disconnectReason() const { return m_disconnectReason; }
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
    const QcPlayerInput &intent() const { return m_intent; }

    // Predicted state after the latest tick and the state one tick earlier, for interpolation.
    const QcEntityMotion &playerMotion() const { return m_motion; }
    const QcEntityMotion &previousPlayerMotion() const { return m_previousMotion; }
    qsizetype pendingInputCount() const { return m_pendingInputs.size(); }
    bool hasServerState() const { return m_hasServerState; }

    // Advances the client by one tick; normally driven by the internal timer.
    void tick();

    // Block the player looks at (within reach), updated every tick.
    const std::optional<QcRaycastHit> &target() const { return m_target; }
    // While true, the targeted block is mined; progress runs from 0 to 1 and resets on target change.
    void setMining(bool mining) { m_miningRequested = mining; }
    double miningProgress() const { return m_miningProgress; }
    // Uses the held item on the targeted block face (places blocks). Returns false without a target.
    bool useItem();
    void selectHotbarSlot(int slot);
    // Slots 0..35 inventory, 36..44 crafting grid (see QcMsg::MoveInventoryItem); count 0 = all.
    void moveInventoryItem(int from, int to, int count = 0);
    // Shift-click: the server moves the stack to where it belongs; the crafting result
    // (QcMsg::QuickMoveItem::craftingResultSlot) is crafted as often as possible.
    void quickMoveItem(int slot);
    void takeCraftingResult();
    void craftRecipe(const QString &recipe, int count = 1);
    void closeCrafting();
    // The machine screen the server opened (position, category), its slots (input, fuel, output)
    // and progress; machineOpen() is false when none is open.
    bool machineOpen() const { return m_machine.has_value(); }
    std::optional<QcBlockPos> machinePos() const { return m_machine; }
    QString machineCategory() const { return m_machineCategory; }
    // The open screen belongs to a storage block (chest) rather than a machine.
    bool containerOpen() const { return m_machine.has_value() && m_machineCategory.isEmpty(); }
    const QList<QcItemStack> &machineSlots() const { return m_machineSlots; }
    double machineBurn() const { return m_machineBurn; }
    double machineProgress() const { return m_machineProgress; }
    void closeMachine();
    const QList<QcItemStack> &craftingGrid() const { return m_craftingGrid; }
    int craftingSize() const { return m_craftingSize; }
    QcItemStack craftingResult() const { return m_craftingResult; }
    void sendChat(const QString &text);
    const QcInventory &inventory() const { return m_inventory; }

    const QHash<quint32, QcClientEntity> &entities() const { return m_entities.all(); }
    // Entity under the crosshair when it is closer than any targeted block.
    std::optional<quint32> targetEntity() const { return m_targetEntity; }
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
    QcEntityPose localPose(double alpha) const;
    struct HandTimers
    {
        double switchProgress = -1.0;
        double landing = -1.0;
        double use = -1.0;
    };
    HandTimers handTimers(double alpha) const;
    // Pose of a mirrored entity from its server state and client timers.
    static QcEntityPose poseOf(const QcClientEntity &entity, double alpha);

    // Animation lengths in ticks.
    static constexpr int swingTicks = QcClientEntities::swingTicks;
    static constexpr int useTicks = 5;
    static constexpr int switchTicks = 6;
    static constexpr int landingTicks = 6;
    static constexpr int deathTicks = QcClientEntities::deathTicks;
    // Throws one item (or the whole stack) of the selected hotbar slot; false when there is none.
    bool dropItem(bool wholeStack);
    int food() const { return m_food; }
    int maxFood() const { return m_maxFood; }
    double health() const { return m_health; }
    double maxHealth() const { return m_maxHealth; }
    bool isDead() const { return m_dead; }
    // Seconds of world time, advanced locally between server updates while the time of day runs.
    double worldTime() const { return m_worldTime; }
    // Labels floating in the loaded chunks (decision E-222) and a number that changes whenever a
    // chunk's labels arrived.
    const QHash<QcChunkPos, QList<QcWorldLabel>> &labels() const { return m_labels; }
    quint64 labelGeneration() const { return m_labelGeneration; }

    // Sounds of the last moments (server events and the client's own), oldest first; at most
    // recentSoundLimit. clearRecentSounds() empties the list (control interface).
    static constexpr qsizetype recentSoundLimit = 64;
    const QList<QcSoundCue> &recentSounds() const { return m_recentSounds; }
    void clearRecentSounds() { m_recentSounds.clear(); }
    // Ambience loop levels around the player, faded towards the surroundings every tick.
    const QcAmbience::Levels &ambience() const { return m_ambience; }
    // Mining strikes make a sound every this many ticks; other creatures step every this many metres.
    static constexpr int miningHitTicks = 4;
    static constexpr double entityStrideMetres = QcClientEntities::strideMetres;
    static constexpr float entityStepVolume = QcClientEntities::stepVolume;
    // The ambience targets are sampled every this many ticks.
    static constexpr int ambienceTicks = 10;

    // Melee reach of the player.
    static constexpr double attackReach = 4.0;
    static constexpr int hurtFlashTicks = QcClientEntities::hurtFlashTicks;
    // Share of the newest tick in the smoothed walking speed of other entities.
    static constexpr double speedSmoothing = QcClientEntities::speedSmoothing;

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
    void machineOpened();
    // Contents or progress of the open machine changed.
    void machineChanged();
    void machineClosed();
    // The server opened a crafting table's grid.
    void craftingOpened(int size);
    void chatReceived(const QString &text);
    // A sound to play now (see QcSoundCue).
    void soundCue(const QcSoundCue &cue);

private:
    void handleMessage(QcMessage &&message);
    // Message groups; each returns false for messages outside its group.
    bool handleConnectionMessage(const QcMessage &message);
    bool handleWorldMessage(const QcMessage &message);
    bool handlePlayerMessage(const QcMessage &message);
    bool handleEntityMessage(const QcMessage &message);
    bool handleMachineMessage(const QcMessage &message);
    void onWelcome(const QcMsg::Welcome &welcome);
    void onContentSync(const QcMsg::ContentSync &sync);
    void onChunkData(const QcMsg::ChunkData &chunkData);
    void onInventoryState(const QcMsg::InventoryState &inventory);
    void onOpenMachine(const QcMsg::OpenMachine &open);
    void onMachineState(const QcMsg::MachineState &machine);
    // Closes the connection; `reason` is reported as the disconnect reason.
    void closeWithError(const QString &reason);
    void setState(State state);
    void reconcile(const QcMsg::PlayerState &state);
    void onClosed();
    void refreshTarget();
    void updateTargetAndMining();
    void cue(QcSoundCue cue);
    void updateSounds();

    std::unique_ptr<QcConnection> m_connection;
    QString m_playerName;
    State m_state = State::Connecting;
    QString m_disconnectReason;
    QString m_worldName;
    qint64 m_seed = 0;
    quint32 m_entityId = 0;
    std::shared_ptr<const QcContentRegistry> m_content;
    std::unique_ptr<QcClientWorld> m_world;
    QTimer *m_tickTimer = nullptr;
    quint64 m_tickCount = 0;

    QcPlayerInput m_intent;
    quint32 m_nextSequence = 1;
    QList<QcPlayerInput> m_pendingInputs;
    QcEntityMotion m_motion;
    QcEntityMotion m_previousMotion;
    bool m_hasServerState = false;
    quint32 m_lastAcknowledged = 0;

    QcInventory m_inventory;
    QList<QcItemStack> m_craftingGrid = QList<QcItemStack>(9);
    int m_craftingSize = 2;
    QcItemStack m_craftingResult;
    std::optional<QcRaycastHit> m_target;
    bool m_miningRequested = false;
    std::optional<QcBlockPos> m_miningPos;
    double m_miningProgress = 0.0;

    QcClientEntities m_entities;
    QcAppearance m_appearance;
    bool m_helloSent = false;
    bool m_appearancePending = false;
    QcCameraMode m_cameraMode = QcCameraMode::FirstPerson;
    // Local animation timers in ticks (negative: not running) and the distance walked.
    int m_swingTicks = -1;
    int m_useTicks = -1;
    int m_switchTicks = -1;
    int m_landingTicks = -1;
    double m_walkDistance = 0.0;
    double m_lastStep = 0.0;
    QcItemId m_lastHeld = qcNoItem;
    std::optional<quint32> m_targetEntity;
    std::optional<QcBlockPos> m_machine;
    // The player closed a machine screen and has not used a block since.
    bool m_machineClosedLocally = false;
    QString m_machineCategory;
    QList<QcItemStack> m_machineSlots = QList<QcItemStack>(QcMsg::MachineState::slotCount);
    double m_machineBurn = 0.0;
    double m_machineProgress = 0.0;
    int m_food = QcItemDefinition::maxFood;
    int m_maxFood = QcItemDefinition::maxFood;
    double m_health = 20.0;
    double m_maxHealth = 20.0;
    bool m_dead = false;
    double m_worldTime = 0.0;
    double m_dayLength = 600.0;
    bool m_timeRunning = true;
    QHash<QcChunkPos, QList<QcWorldLabel>> m_labels;
    quint64 m_labelGeneration = 0;
    QList<QcSoundCue> m_recentSounds;
    QcLocalSounds m_localSounds;
    QcAmbience::Levels m_ambience;
    QcAmbience::Levels m_ambienceTarget;
    int m_miningTicks = 0;
};
