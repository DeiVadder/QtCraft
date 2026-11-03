// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/figures/QcCapeSwing.h"
#include "clientcore/figures/QcEntityPose.h"

#include <QString>

#include <optional>

// The test hook `--pose` for screenshots: a fixed moment of an animation of the local player,
// independent of the real movement ("walk", "walk:<metres>", "sprint", "swing", "swing:<progress>",
// "fly", "flysprint", "swim", "jump", "fall", "sneak", "death", "hurt:<progress>[:<degrees>]").
// "walk:<metres>" also fixes every creature's walk (decision E-888), "hurt:" the tilt of the view
// (decision E-971).
class QcForcedPose
{
public:
    // A moment of a hurt: the share of the hurt time and where the hit came from relative to the
    // view in degrees (none: without a direction).
    struct Hurt
    {
        double progress = 0.0;
        std::optional<double> from;
    };

    void set(const QString &pose) { m_pose = pose; }
    // Whether a pose is forced.
    bool isSet() const { return !m_pose.isEmpty(); }
    // Whether the forced pose is a moment of the mining swing.
    bool swings() const { return m_pose.startsWith(u"swing"); }
    // The metres walked that "walk:<metres>" fixes; nullopt for other poses.
    std::optional<double> walk() const;
    // The hurt that "hurt:<progress>[:<degrees>]" fixes; nullopt for other poses.
    std::optional<Hurt> hurt() const;

    // `pose` (the local player's) with the forced moment; unchanged without one.
    [[nodiscard]] QcEntityPose local(QcEntityPose pose) const;
    // `pose` (a creature's or another player's) at the forced point of the walk.
    [[nodiscard]] QcEntityPose creature(QcEntityPose pose) const;
    // What moves the cape of the forced `pose` facing `yaw`: forwards at its speed, rising after a
    // jump, falling fast.
    [[nodiscard]] QcCapeDrive capeDrive(const QcEntityPose &pose, double yaw) const;

private:
    QString m_pose;
};
