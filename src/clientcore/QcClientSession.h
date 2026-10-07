#pragma once

#include "QcClientEntity.h"
#include "QcClientWorld.h"
#include "QcEntityMotion.h"
#include "QcInventory.h"
#include "QcMessages.h"
#include "QcPlayerInput.h"
#include "QcRaycast.h"

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
    void moveInventoryItem(int from, int to);
    void sendChat(const QString &text);
    const QcInventory &inventory() const { return m_inventory; }

    const QHash<quint32, QcClientEntity> &entities() const { return m_entities; }
    // Entity under the crosshair when it is closer than any targeted block.
    std::optional<quint32> targetEntity() const { return m_targetEntity; }
    // Hits the targeted entity; returns false without one.
    bool attack();
    void respawn();
    double health() const { return m_health; }
    double maxHealth() const { return m_maxHealth; }
    bool isDead() const { return m_dead; }
    // Seconds of world time, advanced locally between server updates.
    double worldTime() const { return m_worldTime; }

    // Melee reach of the player.
    static constexpr double attackReach = 4.0;
    static constexpr int hurtFlashTicks = 6;

signals:
    void stateChanged(QcClientSession::State state);
    void contentReady();
    void chunkLoaded(QcChunkPos pos);
    void chunkUnloaded(QcChunkPos pos);
    void blockChanged(QcBlockPos pos);
    void ticked();
    void disconnected(const QString &reason);
    void inventoryChanged();
    void healthChanged();
    void chatReceived(const QString &text);

private:
    void handleMessage(QcMessage &&message);
    void setState(State state);
    void reconcile(const QcMsg::PlayerState &state);
    void onClosed();
    void refreshTarget();
    void updateTargetAndMining();

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
    std::optional<QcRaycastHit> m_target;
    bool m_miningRequested = false;
    std::optional<QcBlockPos> m_miningPos;
    double m_miningProgress = 0.0;

    QHash<quint32, QcClientEntity> m_entities;
    std::optional<quint32> m_targetEntity;
    double m_health = 20.0;
    double m_maxHealth = 20.0;
    bool m_dead = false;
    double m_worldTime = 0.0;
    double m_dayLength = 600.0;
};
