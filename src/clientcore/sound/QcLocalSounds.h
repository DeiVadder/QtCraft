#pragma once

#include "QcBlockAccess.h"
#include "QcEntityMotion.h"
#include "QcSoundCue.h"

#include <QList>

// Sounds of the local player's own movement, made on the client without a server round trip:
// steps on the block underfoot, jumps, landings, swimming strokes and splashes into water.
// Fed once per client tick with the predicted motion before and after the tick.
class QcLocalSounds
{
public:
    // Distance walked between two steps, and swum between two strokes, in metres.
    static constexpr double strideMetres = 1.7;
    static constexpr double strokeMetres = 2.2;
    // Falling faster than this (m/s) makes a landing louder than a step; entering water faster than
    // splashSpeed makes a splash.
    static constexpr double landingSpeed = 6.0;
    static constexpr double splashSpeed = 3.0;
    static constexpr float stepVolume = 0.45f;
    static constexpr float sneakStepVolume = 0.15f;
    static constexpr float jumpVolume = 0.35f;

    QList<QcSoundCue> update(const QcEntityMotion &before, const QcEntityMotion &after, bool sneaking, const QcBlockAccess &world);
    void reset();

    // The block a standing entity stands on (its feet are on top of it); nullopt when not loaded.
    static std::optional<QcBlockId> groundBelow(const QcVec3 &feet, const QcBlockAccess &world);
    // Step sound of the ground below `feet`, or an empty key when it makes none.
    static QString stepSound(const QcVec3 &feet, const QcBlockAccess &world);

private:
    double m_sinceStep = 0.0;
    double m_sinceStroke = 0.0;
};
