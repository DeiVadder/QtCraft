// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcMusicArrangement.h"

#include <QString>

// Helpers of the music tests (decision E-1156).
namespace QcMusicTestUtil {

// A short layout of the day (two cycles of one-bar chords, about half a minute), so that composing
// and rendering a piece takes well under a second even in a sanitizer build.
inline QcMusicArrangement shortArrangement()
{
    QcMusicArrangement arrangement = *QcMusicArrangement::find(QStringLiteral("day"));
    arrangement.shortestSeconds = 20.0;
    arrangement.longestSeconds = 20.0;
    arrangement.barsPerChord = 1;
    arrangement.fadeInSeconds = 2.0;
    arrangement.fadeOutSeconds = 4.0;
    return arrangement;
}

} // namespace QcMusicTestUtil
