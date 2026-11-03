// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <optional>

struct QcClientEntity;
struct QcMobDefinition;

/*! \brief How a creature that swims in pulses (the squid, QcMobDefinition::pulse, decision E-974)
    holds its body in one frame: tilted by the pitch the server sends (QcCreature::tilt) about the
    middle of its height, its tentacles (QcMobPart::tentacle) open as it slows below the speed of a
    push and pulled in right after one.

    The opening follows the creature's speed over the last tick, so a push closes the tentacles
    and the glide opens them again without a clock shared with the server; a slow sway keeps a
    resting one alive. */
namespace QcSwimmerPose {

/*! \brief The pose of one frame: the \c {tilt} in degrees (0 upright, 90 lying along its yaw)
    about the point \c {height} metres above the feet, and the \c {spread} of the tentacles in
    degrees. \qcfields */
struct Look
{
    double tilt = 0.0;
    double height = 0.0;
    double spread = 0.0;
};

/*! Returns the share (0..1) of the tentacles' opening that the screenshot hook
    \c {--pose pulse:<0..1>} in \a forcedPose holds; nothing for any other pose. */
[[nodiscard]] std::optional<double> forcedOpening(const QString &forcedPose);

/*! Returns the opening (0 pulled in, 1 fully open) of a swimmer moving at \a speed m/s whose
    push reaches \a pushSpeed, swaying by the animation clock \a seconds with the phase \a phase.
    Pure. */
[[nodiscard]] double opening(double speed, double pushSpeed, double seconds, double phase);

/*! Returns the pose of \a entity, of the kind \a definition (which swims in pulses), at \a alpha
    between the last two ticks and the animation clock \a seconds, with the tentacles held at
    \a forced (forcedOpening()) when given. */
[[nodiscard]] Look of(const QcMobDefinition &definition, const QcClientEntity &entity, double alpha, double seconds, std::optional<double> forced);

} // namespace QcSwimmerPose
