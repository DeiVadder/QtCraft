#pragma once

#include "QcClientWorld.h"
#include "QcEntityMotion.h"
#include "QcMessages.h"
#include "QcPlayerInput.h"

#include <QList>
#include <QObject>

#include <memory>

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

signals:
    void stateChanged(QcClientSession::State state);
    void contentReady();
    void chunkLoaded(QcChunkPos pos);
    void chunkUnloaded(QcChunkPos pos);
    void blockChanged(QcBlockPos pos);
    void ticked();
    void disconnected(const QString &reason);

private:
    void handleMessage(QcMessage &&message);
    void setState(State state);
    void reconcile(const QcMsg::PlayerState &state);
    void onClosed();

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
};
