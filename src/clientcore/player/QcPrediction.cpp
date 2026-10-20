#include "clientcore/player/QcPrediction.h"

#include "common/physics/QcPhysics.h"

#include <algorithm>
#include <cmath>

void QcPrediction::setIntent(const QcPlayerInput &intent)
{
    const auto finiteOr = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
    m_intent = intent;
    m_intent.forward = std::clamp(finiteOr(intent.forward, 0.0f), -1.0f, 1.0f);
    m_intent.strafe = std::clamp(finiteOr(intent.strafe, 0.0f), -1.0f, 1.0f);
    m_intent.pitch = std::clamp(finiteOr(intent.pitch, 0.0f), -90.0f, 90.0f);
    m_intent.flySpeed = std::clamp(intent.flySpeed, QcPlayerInput::minFlySpeed, QcPlayerInput::maxFlySpeed);
    const float yaw = std::fmod(finiteOr(intent.yaw, 0.0f), 360.0f);
    m_intent.yaw = yaw < 0.0f ? yaw + 360.0f : yaw;
}

QcPlayerInput QcPrediction::predict(const QcBlockAccess &world, bool flying, QcMovement movement)
{
    QcPlayerInput input = m_intent;
    input.sequence = m_nextSequence++;
    input.flying = flying;
    m_previousMotion = m_motion;
    if (!m_riding.isRiding()) {
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
    const bool gotInOrOut = m_riding.noteVehicle(state.vehicle);
    const Reconciled result = placed ? Reconciled::Placed : Reconciled::Applied;
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
    m_intent = intent;
    if (m_riding.isRiding()) {
        m_motion = motion;
        m_previousMotion = previous;
    } else if (wasRiding) {
        // Got out: the core's motion before is where the player got in, the own one the seat.
        m_previousMotion = previous;
    }
    if (!current) {
        m_largestCorrection = largest;
    }
}
