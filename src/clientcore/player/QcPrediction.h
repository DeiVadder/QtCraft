// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/player/QcClientRiding.h"
#include "common/physics/QcBoatMotion.h"
#include "common/physics/QcEntityMotion.h"
#include "common/physics/QcPlayerInput.h"
#include "common/protocol/QcMessages.h"

#include <QList>

#include <optional>

class QcBlockAccess;
class QcClientEntities;

/*! \brief Prediction of the local player: the movement intent, the inputs the server has not
    acknowledged yet, the predicted motion one tick ago and now, and the reconciliation with the
    authoritative state (corrections, decision E-257).

    While the player rides a minecart nothing is predicted, the player follows the cart
    (QcClientRiding, decision E-382). While they drive a boat the boat is predicted by the shared
    boat physics (QcBoatState, decision E-920) from the server's state of it and the player sits
    on its seat. Plain data without Qt objects or signals: it reads the world and the entity
    mirror only during predict() and reconcile(). */
class QcPrediction
{
public:
    /*! \brief What a server state did to the prediction. */
    enum class Reconciled {
        /*! Older than a state already applied; nothing changed. */
        Ignored,
        /*! The first state: it placed the player in the world. */
        Placed,
        /*! A later state: the pending inputs were replayed on top of it. */
        Applied,
    };

    /*! Unacknowledged inputs beyond this are dropped; the server would reject them anyway. */
    static constexpr qsizetype maxPendingInputs = 100;

    /*! Sets the movement \a intent applied on every tick until changed. Non-finite values are
        replaced, ranges (also the flight speed) clamped and yaw wrapped into [0, 360). */
    void setIntent(const QcPlayerInput &intent);
    /*! Returns the movement intent as last set. */
    const QcPlayerInput &intent() const { return m_intent; }

    /*! Returns the predicted state after the latest tick. */
    const QcEntityMotion &motion() const { return m_motion; }
    /*! Returns the predicted state one tick earlier, for interpolation. */
    const QcEntityMotion &previousMotion() const { return m_previousMotion; }
    /*! Returns the number of inputs the server has not acknowledged yet. */
    qsizetype pendingInputCount() const { return m_pendingInputs.size(); }
    /*! Returns the sequence number of the latest input predicted; 0 before the first. */
    quint32 lastSequence() const { return m_nextSequence - 1; }
    /*! Returns the sequence number of the latest input the server acknowledged. */
    quint32 lastAcknowledged() const { return m_lastAcknowledged; }
    /*! Returns the metres the server moved the predicted player in the latest reconcile (E-257). */
    double lastCorrection() const { return m_lastCorrection; }
    /*! Returns the most metres the server moved the predicted player since resetCorrections(). */
    double largestCorrection() const { return m_largestCorrection; }
    /*! Forgets the largest correction so far. */
    void resetCorrections() { m_largestCorrection = 0.0; }
    /*! Returns whether a state of the server placed the player. */
    bool hasServerState() const { return m_hasServerState; }
    /*! Returns the minecart the player rides or the boat they drive. */
    const QcClientRiding &riding() const { return m_riding; }
    /*! Returns whether the player crouches: he sneaks on foot, not flying (sneak descends then,
        decision E-284) and not riding. */
    [[nodiscard]] bool crouches() const;
    /*! Returns the height of the player's eyes above the feet after the latest tick: lower while
        he crouches (QcPhysics::eyeHeight, decision E-961). */
    [[nodiscard]] double eyeHeight() const;
    /*! Returns the predicted boat the player drives after the latest tick; none when they drive
        none. */
    const std::optional<QcBoatState> &boat() const { return m_boat; }
    /*! Returns the predicted boat one tick earlier. */
    const std::optional<QcBoatState> &previousBoat() const { return m_previousBoat; }

    /*! Predicts one tick in \a world from the intent: on foot or flying (with \a flying as the
        wish sent and \a movement from the game mode) through the shared physics, pushed by the
        creatures with \a push (QcPushBodies, decision E-882; kept with the input, so a replay
        pushes alike: after a correction the replayed steps keep the pushes of the places
        predicted before, not of the corrected ones, and the next correction takes up the
        difference); riding a cart the motion stays (followCart moves it); driving a boat the
        input steps the boat and the player sits in it. Returns the input to send; it carries the
        next sequence number and is kept until acknowledged. */
    QcPlayerInput predict(const QcBlockAccess &world, bool flying, QcMovement movement, const QcVec3 &push = {});
    /*! Sets \a effects, what the player's status effects do to his steps, as the server told them
        with the count \a epoch (decision E-992); the next inputs carry both, so replaying them
        moves as predicted and the server steps them alike. */
    void setMovementEffects(QcMovementEffects effects, quint16 epoch)
    {
        m_intent.effects = effects;
        m_intent.movementEpoch = epoch;
    }
    /*! Sets \a usingItem, whether the next inputs hold an item in use (draw a bow, decision
        E-986). The inputs keep it, so replaying them moves as predicted. */
    void setUsingItem(bool usingItem);
    /*! Returns whether \a state shows that the server does not know of the running draw: it
        acknowledged an input of this draw and does not draw. */
    [[nodiscard]] bool usingItemRefused(const QcMsg::PlayerState &state) const;
    /*! While riding, after predict(): the player moves onto the seat of the cart as the entity
        mirror \a entities shows it this tick. */
    void followCart(const QcClientEntities &entities);
    /*! A tick without a step (the server paused or the player dead, decision E-960): the state one
        tick earlier becomes the current one, so the interpolation between them stands still
        instead of swinging back over the last step with every tick. */
    void rest();
    /*! Takes over the prediction of the session's tick \a core (decision E-560), keeping the own
        intent (the session writes it) and, while riding, the own motion (the session follows the
        cart). With \a current false the core has not seen all of the session's commands yet: the
        own largest correction stays (it may just have been reset). */
    void mirror(const QcPrediction &core, bool current);
    /*! Applies the server's \a state of the player and replays the pending inputs on top of it
        (with \a world once it exists, moving by \a movement). Returns what the state did. */
    Reconciled reconcile(const QcMsg::PlayerState &state, const QcBlockAccess *world, QcMovement movement);

private:
    // The motion of the driver on the seat of `boat`.
    [[nodiscard]] static QcEntityMotion seatedIn(const QcBoatState &boat);
    void setBoat(const std::optional<QcBoatState> &boat) { m_boat = boat; }
    void setPreviousBoat(const std::optional<QcBoatState> &boat) { m_previousBoat = boat; }
    void setPreviousMotion(const QcEntityMotion &motion) { m_previousMotion = motion; }
    // Applies a state that names a boat: its state replayed with the pending inputs.
    void reconcileBoat(const QcBoatState &boat, const QcBlockAccess *world, bool placedOrMoved);

    // Copies what the core alone sets in the intent, from `core`.
    void keepCoreIntent(const QcPlayerInput &core);

    QcPlayerInput m_intent;
    quint32 m_nextSequence = 1;
    // The first input of the running draw.
    quint32 m_usingItemSince = 0;
    QList<QcPlayerInput> m_pendingInputs;
    QcEntityMotion m_motion;
    QcEntityMotion m_previousMotion;
    bool m_hasServerState = false;
    double m_lastCorrection = 0.0;
    double m_largestCorrection = 0.0;
    quint32 m_lastAcknowledged = 0;
    QcClientRiding m_riding;
    std::optional<QcBoatState> m_boat;
    std::optional<QcBoatState> m_previousBoat;
};
