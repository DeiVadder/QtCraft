// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcAtmosphere.h"

#include <QJsonObject>
#include <QJsonValue>

struct QcJsonFieldReader;

/*! \brief Parses the atmosphere of a biome and the particles of a block (decisions E-1120,
    E-1121). */
namespace QcAtmosphereParser {

/*! Limits of the music style: MIDI notes of the root, the tempo in beats per minute and the
    cut-off of the melody's low-pass in Hz. \qcshared */
inline constexpr int lowestRoot = 24;
inline constexpr int highestRoot = 72;
inline constexpr int slowestTempo = 30;
inline constexpr int fastestTempo = 160;
inline constexpr double lowestLowpass = 200.0;
inline constexpr double highestLowpass = 8000.0;

/*! Returns the atmosphere in \a json (a biome's \c {"atmosphere"} object), read with \a reader;
    sound names without a namespace get \a packNamespace. Problems fail the reader. */
[[nodiscard]] QcBiomeAtmosphere parseAtmosphere(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace);
/*! Returns the particle emitters in \a value (a block's \c {"particles"} array, at most four);
    a missing value gives none, problems fail \a reader. */
[[nodiscard]] QList<QcParticleEmitter> parseEmitters(const QJsonValue &value, QcJsonFieldReader &reader);

} // namespace QcAtmosphereParser
