// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcLocalAnimation.h"

#include "clientcore/player/QcPrediction.h"
#include "common/physics/QcPhysics.h"

#include <algorithm>
#include <cmath>

void QcLocalAnimation::tick(QcItemId held, bool mining, QcSwingSpeed speed, double draw)
{
    const auto advance = [](int &ticks, int length) {
        if (ticks < 0) {
            return;
        }
        ++ticks;
        if (ticks >= length) {
            ticks = -1;
        }
    };

    advance(m_swingTicks, m_swingLength);
    advance(m_useTicks, QcHandTimers::useTicks);
    advance(m_switchTicks, QcHandTimers::switchTicks);
    advance(m_landingTicks, QcHandTimers::landingTicks);
    advance(m_hurtTicks, hurtTicks);

    // The hand comes to a drawn launcher and leaves it again over aimTicks (decision E-984).
    setAimTicks(aimTicks() + (drawing() ? 1 : -1));
    setDrawing(draw >= 0.0);
    if (drawing()) {
        setRunningDraw(std::min(draw, 1.0));
        setLastDraw(std::max(runningDraw(), lastDraw() - lastDrawFade));
    } else if (aimTicks() == 0) {
        setLastDraw(0.0);
    }

    if (held != m_lastHeld) {
        m_lastHeld = held;
        m_switchTicks = 0;
        // The pose of a launcher does not run on over the new item.
        setAimTicks(0);
        setDrawing(false);
        setLastDraw(0.0);
    }

    // Keep swinging while a block is being mined: the next swing starts in the tick the last one
    // ends, so the loop has no pause and its period is the swing's length, like the server's
    // cadence of swings (QcServerInteraction::swingWhileMining).
    m_nextSwingLength = speed.ticks();
    if (mining && m_swingTicks < 0) {
        startSwing(QcHandSwing::Style::Mining);
    }
}

void QcLocalAnimation::stepped(const QcEntityMotion &previous, const QcEntityMotion &current)
{
    m_lastStep = std::hypot(current.position.x - previous.position.x, current.position.z - previous.position.z);
    m_walkDistance += m_lastStep;
    if (current.onGround && !previous.onGround) {
        m_landingTicks = 0;
    }
}

QcEntityPose QcLocalAnimation::localPose(double alpha, const QcPrediction &prediction, double drawProgress) const
{
    const QcEntityMotion &motion = prediction.motion();
    const QcPlayerInput &intent = prediction.intent();
    QcEntityPose pose;
    pose.speed = m_lastStep * QcPhysics::tickRate;
    pose.walkDistance = m_walkDistance - m_lastStep * (1.0 - alpha);
    pose.onGround = motion.onGround;
    pose.inWater = motion.inWater;
    // Sneaking in flight is descending, not crouching (decision E-284).
    pose.sneaking = prediction.crouches();
    pose.sprinting = intent.sprint && intent.forward > 0.0f && !intent.sneak;
    pose.flying = motion.flying;
    pose.riding = prediction.riding().isRiding();
    pose.swingProgress = m_swingTicks < 0 ? -1.0 : (m_swingTicks + alpha) / m_swingLength;
    pose.hurtProgress = m_hurtTicks < 0 ? -1.0 : (m_hurtTicks + alpha) / hurtTicks;
    pose.drawProgress = drawProgress;
    pose.headPitch = intent.pitch;
    return pose;
}

void QcLocalAnimation::hurt(std::optional<float> from, float viewYaw)
{
    m_hurtTicks = 0;
    m_hurtFrom.reset();
    if (from) {
        m_hurtFrom = static_cast<double>(qcWrappedYaw(*from - viewYaw));
    }
}

QcHurtTilt::Tilt QcLocalAnimation::hurtTilt(double alpha) const
{
    if (m_hurtTicks < 0) {
        return {};
    }
    return QcHurtTilt::at((m_hurtTicks + alpha) / hurtTicks, m_hurtFrom);
}

QcHandTimers QcLocalAnimation::handTimers(double alpha) const
{
    const auto progress = [alpha](int ticks, int length) { return ticks < 0 ? -1.0 : (ticks + alpha) / length; };
    const double aimed = aimTicks() + (drawing() ? alpha : -alpha);
    const bool fading = drawing() && lastDraw() > runningDraw();
    return {progress(m_switchTicks, QcHandTimers::switchTicks),
            progress(m_landingTicks, QcHandTimers::landingTicks),
            progress(m_useTicks, QcHandTimers::useTicks),
            m_swingTicks >= 0 && m_swingStyle == QcHandSwing::Style::Mining,
            std::clamp(aimed / QcHandTimers::aimTicks, 0.0, 1.0),
            fading ? std::max(runningDraw(), lastDraw() - lastDrawFade * alpha) : lastDraw()};
}

QcEntityPose QcLocalAnimation::poseOf(const QcClientEntity &entity, double alpha)
{
    QcEntityPose pose;
    pose.speed = entity.speed;
    pose.walkDistance = entity.walkDistance - entity.lastStep * (1.0 - alpha);
    pose.onGround = (entity.flags & QcMsg::OnGround) != 0;
    pose.inWater = (entity.flags & QcMsg::InWater) != 0;
    pose.sneaking = (entity.flags & QcMsg::Sneaking) != 0;
    pose.sprinting = (entity.flags & QcMsg::Sprinting) != 0;
    pose.riding = (entity.flags & QcMsg::Riding) != 0;
    pose.body = QcMsg::poseOf(entity.flags);
    pose.swingProgress = entity.swingTicks < 0 ? -1.0 : (entity.swingTicks + alpha) / swingTicks;
    pose.hurtProgress = entity.hurtTicks > 0 ? 1.0 - (entity.hurtTicks - alpha) / QcClientEntities::hurtFlashTicks : -1.0;
    pose.deathProgress = entity.deathTicks < 0 ? -1.0 : std::min(1.0, (entity.deathTicks + alpha) / deathTicks);
    pose.headPitch = entity.previousPitch + (entity.pitch - entity.previousPitch) * static_cast<float>(alpha);
    // Others only tell that they draw (decision E-425): shown fully drawn.
    pose.drawProgress = (entity.flags & QcMsg::Drawing) != 0 ? 1.0 : -1.0;
    return pose;
}
