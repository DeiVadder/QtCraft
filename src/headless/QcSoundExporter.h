// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QString>
#include <QStringList>

class QcContentRegistry;

/*! \brief Development aid: writes every sound of the content (first variant) and one composed
    music piece per place of QcMusicArrangement (menu, day, night, cave, the nether's biomes) as
    16-bit mono WAV files, so that sounds and music can be listened to and checked without the
    game (\c {scripts/music-traits.py} measures the pieces). */
namespace QcSoundExporter {

/*! Writes every sound of \a content and, per place, the music piece composed from \a musicSeed
    (\c {music-<place>-<seed>.wav}) into \a directory (creating it). Returns one line per file:
    name, length in seconds and peak level; an error when a file cannot be written. */
[[nodiscard]] QcResult<QStringList> exportAll(const QcContentRegistry &content, const QString &directory, quint64 musicSeed);

} // namespace QcSoundExporter
