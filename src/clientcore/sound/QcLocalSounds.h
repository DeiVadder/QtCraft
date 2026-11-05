// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcSoundCue.h"
#include "common/physics/QcEntityMotion.h"
#include "common/world/QcBlockAccess.h"

#include <QList>

/*! \brief Sounds of the local player's own movement, made on the client without a server round
    trip: steps on the block underfoot, jumps, landings, swimming strokes and splashes into water.

    Fed once per client tick with the predicted motion before and after the tick. */
class QcLocalSounds
{
public:
    /*! Distance walked between two steps, and swum between two strokes, in metres. \qcshared */
    static constexpr double strideMetres = 1.7;
    static constexpr double strokeMetres = 2.2;
    /*! Falling faster than this (m/s) makes a landing louder than a step. */
    static constexpr double landingSpeed = 6.0;
    /*! Entering water faster than this (m/s) makes a splash. */
    static constexpr double splashSpeed = 3.0;
    /*! Volume of a step. */
    static constexpr float stepVolume = 0.45f;
    /*! Volume of a step while sneaking. */
    static constexpr float sneakStepVolume = 0.15f;
    /*! Volume of a jump. */
    static constexpr float jumpVolume = 0.35f;

    /*! Returns the sounds of the tick from motion \a before to motion \a after, on the ground of \a
        world; quieter steps while \a sneaking. */
    QList<QcSoundCue> update(const QcEntityMotion &before, const QcEntityMotion &after, bool sneaking, const QcBlockAccess &world);

    /*! Returns the block of \a world a standing entity stands on (its \a feet are on top of it);
        nullopt when not loaded. */
    [[nodiscard]] static std::optional<QcBlockId> groundBelow(const QcVec3 &feet, const QcBlockAccess &world);
    /*! Returns the step sound of the ground of \a world below \a feet, or an empty key when it
        makes none. */
    static QString stepSound(const QcVec3 &feet, const QcBlockAccess &world);

private:
    double m_sinceStep = 0.0;
    double m_sinceStroke = 0.0;
};
