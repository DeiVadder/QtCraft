// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcPrediction.h"

#include "common/physics/QcPhysics.h"
#include "common/world/QcBoatBody.h"

#include <algorithm>
#include <cmath>

void QcPrediction::setIntent(const QcPlayerInput &intent)
{
    const auto finiteOr = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
    const float speedFactor = m_intent.speedFactor;
    m_intent = intent;
    m_intent.speedFactor = speedFactor;
    m_intent.forward = std::clamp(finiteOr(intent.forward, 0.0f), -1.0f, 1.0f);
    m_intent.strafe = std::clamp(finiteOr(intent.strafe, 0.0f), -1.0f, 1.0f);
    m_intent.pitch = std::clamp(finiteOr(intent.pitch, 0.0f), -90.0f, 90.0f);
    m_intent.flySpeed = std::clamp(intent.flySpeed, QcPlayerInput::minFlySpeed, QcPlayerInput::maxFlySpeed);
    m_intent.yaw = qcWrappedYaw(intent.yaw);
}

QcPlayerInput QcPrediction::predict(const QcBlockAccess &world, bool flying, QcMovement movement, const QcVec3 &push)
{
    QcPlayerInput input = m_intent;
    input.sequence = m_nextSequence++;
    input.flying = flying;
    input.push = push;
    m_previousMotion = m_motion;
    if (m_riding.drivesBoat() && boat()) {
        // The same step the server makes with this input (QcServerBoats::steer).
        QcBoatState stepped = *boat();
        setPreviousBoat(stepped);
        stepped.step(QcBoatControls::of(input), world);
        setBoat(stepped);
        m_motion = seatedIn(stepped);
    } else if (!m_riding.isRiding()) {
        m_motion.stepPlayer(input, world, QcPhysics::tickDuration, movement);
    }

    if (m_pendingInputs.size() >= maxPendingInputs) {
        m_pendingInputs.removeFirst();
    }
    m_pendingInputs.append(input);
    return input;
}

QcPrediction::Reconciled QcPrediction::reconcile(const QcMsg::PlayerState &state, const QcBlockAccess *world, QcMovement movement)
{
    // A state older than one already applied would roll the prediction back; ignore it.
    if (m_hasServerState && state.lastInputSequence < m_lastAcknowledged) {
        return Reconciled::Ignored;
    }

    // The first state places the player in the world: it corrects no prediction.
    const bool placed = !m_hasServerState;
    m_lastAcknowledged = state.lastInputSequence;
    m_hasServerState = true;
    m_pendingInputs.removeIf([&state](const QcPlayerInput &input) { return input.sequence <= state.lastInputSequence; });
    // A rider sits in the cart as the mirror shows it; getting in or out is a placement by the
    // server, not a correction (decision E-382).
    const bool gotInOrOut = m_riding.noteVehicle(state.vehicle, state.boat.has_value());
    const Reconciled result = placed ? Reconciled::Placed : Reconciled::Applied;
    if (state.boat) {
        reconcileBoat(*state.boat, world, placed || gotInOrOut);
        return result;
    }
    setBoat(std::nullopt);
    setPreviousBoat(std::nullopt);
    if (m_riding.isRiding()) {
        m_lastCorrection = 0.0;
        return result;
    }

    QcEntityMotion replayed = state.motion;
    if (world) {
        for (const QcPlayerInput &input : std::as_const(m_pendingInputs)) {
            replayed.stepPlayer(input, *world, QcPhysics::tickDuration, movement);
        }
    }
    m_lastCorrection = gotInOrOut || placed ? 0.0 : (replayed.position - m_motion.position).length();
    m_largestCorrection = std::max(m_largestCorrection, m_lastCorrection);
    m_motion = replayed;
    return result;
}

QcEntityMotion QcPrediction::seatedIn(const QcBoatState &boat)
{
    QcEntityMotion motion;
    motion.position = QcBoatBody::driverSeat(boat.motion.position, boat.yaw);
    motion.velocity = boat.motion.velocity;
    motion.onGround = true;
    return motion;
}

void QcPrediction::reconcileBoat(const QcBoatState &boat, const QcBlockAccess *world, bool placedOrMoved)
{
    QcBoatState replayed = boat;
    if (world) {
        for (const QcPlayerInput &input : std::as_const(m_pendingInputs)) {
            replayed.step(QcBoatControls::of(input), *world);
        }
    }
    // Getting in is a placement by the server; afterwards the boat moves where it was predicted.
    const std::optional<QcBoatState> &predicted = this->boat();
    const bool placed = placedOrMoved || !predicted;
    m_lastCorrection = 0.0;
    if (!placed && predicted) {
        m_lastCorrection = (replayed.motion.position - predicted->motion.position).length();
    }
    m_largestCorrection = std::max(m_largestCorrection, m_lastCorrection);
    if (placed) {
        setPreviousBoat(replayed);
    }
    setBoat(replayed);
    m_motion = seatedIn(replayed);
}

void QcPrediction::followCart(const QcClientEntities &entities)
{
    m_previousMotion = m_motion;
    if (const std::optional<QcEntityMotion> seated = m_riding.seated(entities)) {
        m_motion = *seated;
    }
}

void QcPrediction::mirror(const QcPrediction &core, bool current)
{
    const QcPlayerInput intent = m_intent;
    const QcEntityMotion motion = m_motion;
    const QcEntityMotion previous = m_previousMotion;
    const double largest = m_largestCorrection;
    const bool wasRiding = m_riding.isRiding();
    *this = core;
    const float speedFactor = m_intent.speedFactor;
    m_intent = intent;
    m_intent.speedFactor = speedFactor;
    if (m_riding.ridesCart()) {
        m_motion = motion;
        m_previousMotion = previous;
    } else if (wasRiding && !m_riding.drivesBoat()) {
        // Got out: the core's motion before is where the player got in, the own one the seat.
        m_previousMotion = previous;
    }
    if (!current) {
        m_largestCorrection = largest;
    }
}
