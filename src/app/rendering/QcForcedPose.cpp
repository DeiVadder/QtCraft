// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/rendering/QcForcedPose.h"

#include "clientcore/figures/QcAnimator.h"
#include "common/physics/QcPhysics.h"

#include <algorithm>

using namespace Qt::StringLiterals;

std::optional<double> QcForcedPose::walk() const
{
    if (!m_pose.startsWith(u"walk:"_s)) {
        return std::nullopt;
    }
    bool ok = false;
    const double walked = m_pose.section(u':', 1).toDouble(&ok);
    return ok ? std::optional(walked) : std::nullopt;
}

std::optional<QcForcedPose::Hurt> QcForcedPose::hurt() const
{
    if (!m_pose.startsWith(u"hurt:"_s)) {
        return std::nullopt;
    }
    bool ok = false;
    const double progress = m_pose.section(u':', 1, 1).toDouble(&ok);
    if (!ok) {
        return std::nullopt;
    }
    bool directed = false;
    const double from = m_pose.section(u':', 2, 2).toDouble(&directed);
    return Hurt{std::clamp(progress, 0.0, 1.0), directed ? std::optional(from) : std::nullopt};
}

QcEntityPose QcForcedPose::creature(QcEntityPose pose) const
{
    // Every creature a fixed moment of its walk at full swing (decision E-888).
    if (const std::optional<double> walked = walk()) {
        pose.walkDistance = *walked;
        pose.speed = std::max(QcAnimator::fullSwingSpeed, QcPhysics::walkSpeed);
        pose.onGround = true;
    }
    return pose;
}

QcEntityPose QcForcedPose::local(QcEntityPose pose) const
{
    if (!isSet()) {
        return pose;
    }

    pose.onGround = true;
    if (m_pose == u"walk"_s || walk()) {
        pose.speed = QcPhysics::walkSpeed;
        pose.walkDistance = walk().value_or(0.3);
    } else if (m_pose == u"sprint"_s) {
        pose.speed = QcPhysics::sprintSpeed;
        pose.sprinting = true;
        pose.walkDistance = 0.3;
    } else if (swings()) {
        // "swing:0.3" freezes that moment of the swing (screenshot series).
        bool ok = false;
        const double progress = m_pose.section(u':', 1).toDouble(&ok);
        pose.swingProgress = ok ? std::clamp(progress, 0.0, 1.0) : 0.45;
    } else if (m_pose == u"fly"_s || m_pose == u"flysprint"_s) {
        // Creative flight (decision E-284), with sprint the widest view.
        pose.onGround = false;
        pose.flying = true;
        pose.sprinting = m_pose == u"flysprint"_s;
        pose.speed = pose.sprinting ? QcPhysics::flySprintSpeed : QcPhysics::flySpeed;
    } else if (m_pose == u"swim"_s) {
        pose.onGround = false;
        pose.inWater = true;
        pose.speed = QcPhysics::swimSpeed;
    } else if (m_pose == u"jump"_s || m_pose == u"fall"_s) {
        pose.onGround = false;
    } else if (m_pose == u"sneak"_s) {
        pose.sneaking = true;
    } else if (m_pose == u"death"_s) {
        pose.deathProgress = 0.7;
    } else if (const std::optional<Hurt> forced = hurt()) {
        pose.hurtProgress = forced->progress;
    }
    return pose;
}

QcCapeDrive QcForcedPose::capeDrive(const QcEntityPose &pose, double yaw) const
{
    QcCapeDrive drive = QcCapeDrive::from({}, yaw, pose);
    drive.forwardSpeed = pose.speed;
    if (m_pose == u"jump"_s) {
        drive.verticalSpeed = QcPhysics::jumpVelocity / 2.0;
    } else if (m_pose == u"fall"_s) {
        drive.verticalSpeed = -QcPhysics::jumpVelocity * 2.0;
    }
    return drive;
}
