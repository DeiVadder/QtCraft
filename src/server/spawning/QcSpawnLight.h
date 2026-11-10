// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcDayCycle.h"

#include <QRandomGenerator>

/*! \brief The light test of monsters (decision E-836), as in the original game: a cell is dark
    enough when its sky light passes a random draw of 0..31, its block light (torches) is at most
    the rule's \c {maxLight} (0 for monsters) and its brightness, the larger of block light and the
    sky light less the darkening of the sky by time and weather, is at most a random threshold of
    0..7.

    Day or night is no condition of its own: by day the open sky is far too bright, while caves, the
    shade of thick roofs and dense forests stay dark enough; at night the dimmed sky lets most open
    ground pass, the more often the darker it is. Spawn rules with \c {"darkness": false} (slime
    chunks) skip the test. */
namespace QcSpawnLight {

/*! Levels the sky light loses at night (midnight in clear weather). */
inline constexpr int nightDarkening = QcDayCycle::nightDarkening;
/*! Levels it loses during a thunderstorm, by day and by night. */
inline constexpr int thunderDarkening = 10;
/*! A thunderstorm darkens the sky by thunderDarkening from this strength on. */
inline constexpr float thunderStrength = 0.9f;
/*! The threshold the brightness is compared with lies in 0..maxThreshold. */
inline constexpr int maxThreshold = 7;
/*! The sky light must not exceed a draw of 0..skyChance - 1. */
inline constexpr int skyChance = 32;

/*! Returns the levels the sky light loses at \a worldTime with \a rain and \a thunder of strengths
    0..1: 0 at noon in clear weather, nightDarkening at midnight, rain dims the day a little, a
    storm as far as thunderDarkening. */
int skyDarkening(double worldTime, float rain, float thunder);

/*! \brief The random draws of one test: the sky's draw (0..skyChance - 1) and the threshold
    (0..maxThreshold). */
struct Draws
{
    /*! The sky's draw. */
    int sky = 0;
    /*! The threshold. */
    int threshold = 0;
};
/*! Returns the draws of one test from \a random. */
Draws draw(QRandomGenerator &random);

/*! \brief The sky light, block light and darkening of a cell, and the brightest block light
    allowed. */
struct Cell
{
    /*! Its sky light. */
    int skyLight = 0;
    /*! Its block light. */
    int blockLight = 0;
    /*! How much the sky is darkened. */
    int darkening = 0;
    /*! The brightest block light allowed. */
    int maxBlockLight = 0;
};
/*! Returns whether \a cell is dark enough for a monster with \a draws. */
bool darkEnough(Cell cell, Draws draws);

} // namespace QcSpawnLight
