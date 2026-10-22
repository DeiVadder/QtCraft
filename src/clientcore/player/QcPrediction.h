#pragma once

#include "clientcore/player/QcClientRiding.h"
#include "common/physics/QcEntityMotion.h"
#include "common/physics/QcPlayerInput.h"
#include "common/protocol/QcMessages.h"

#include <QList>

class QcBlockAccess;
class QcClientEntities;

// Prediction of the local player: the movement intent, the inputs the server has not acknowledged
// yet, the predicted motion one tick ago and now, and the reconciliation with the authoritative
// state (corrections, decision E-257). While the player rides a minecart nothing is predicted, the
// player follows the cart (QcClientRiding, decision E-382). Plain data without Qt objects or
// signals: it reads the world and the entity mirror only during predict() and reconcile().
class QcPrediction
{
public:
    // What a server state did to the prediction.
    enum class Reconciled {
        // Older than a state already applied; nothing changed.
        Ignored,
        // The first state: it placed the player in the world.
        Placed,
        // A later state: the pending inputs were replayed on top of it.
        Applied,
    };

    // Unacknowledged inputs beyond this are dropped; the server would reject them anyway.
    static constexpr qsizetype maxPendingInputs = 100;

    // Movement intent applied on every tick until changed. Non-finite values are replaced, ranges
    // (also the flight speed) clamped and yaw wrapped into [0, 360).
    void setIntent(const QcPlayerInput &intent);
    const QcPlayerInput &intent() const { return m_intent; }

    // Predicted state after the latest tick and the state one tick earlier, for interpolation.
    const QcEntityMotion &motion() const { return m_motion; }
    const QcEntityMotion &previousMotion() const { return m_previousMotion; }
    qsizetype pendingInputCount() const { return m_pendingInputs.size(); }
    // Metres the server moved the predicted player: latest reconcile, most since reset (E-257).
    double lastCorrection() const { return m_lastCorrection; }
    double largestCorrection() const { return m_largestCorrection; }
    void resetCorrections() { m_largestCorrection = 0.0; }
    bool hasServerState() const { return m_hasServerState; }
    // The minecart the player rides.
    const QcClientRiding &riding() const { return m_riding; }

    // Predicts one tick from the intent: on foot or flying (with `flying` as the wish sent and
    // `movement` from the game mode) through the shared physics; riding the motion stays (followCart
    // moves it). Returns the input to send; it carries the next sequence number and is kept until
    // acknowledged.
    QcPlayerInput predict(const QcBlockAccess &world, bool flying, QcMovement movement);
    // The factor of the player's status effects on his speed (slowness, decision E-838), as the
    // server told them; the next inputs carry it, so replaying them moves as predicted.
    void setSpeedFactor(float factor) { m_intent.speedFactor = factor; }
    // While riding, after predict(): the player moves onto the seat of the cart as the entity
    // mirror shows it this tick.
    void followCart(const QcClientEntities &entities);
    // Takes over the prediction of the session's tick core (decision E-560), keeping the own intent
    // (the session writes it) and, while riding, the own motion (the session follows the cart). With
    // `current` false the core has not seen all of the session's commands yet: the own largest
    // correction stays (it may just have been reset).
    void mirror(const QcPrediction &core, bool current);
    // Applies the server's state of the player and replays the pending inputs on top of it (with
    // the world once it exists).
    Reconciled reconcile(const QcMsg::PlayerState &state, const QcBlockAccess *world, QcMovement movement);

private:
    QcPlayerInput m_intent;
    quint32 m_nextSequence = 1;
    QList<QcPlayerInput> m_pendingInputs;
    QcEntityMotion m_motion;
    QcEntityMotion m_previousMotion;
    bool m_hasServerState = false;
    double m_lastCorrection = 0.0;
    double m_largestCorrection = 0.0;
    quint32 m_lastAcknowledged = 0;
    QcClientRiding m_riding;
};
