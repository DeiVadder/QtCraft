// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <optional>

/*! \brief Where the music comes from (decision E-1137): the tracks of the content packs (Scott
    Buckley's in the base game) or pieces the game composes (QcMusicComposer). Stored as
    "audio/musicSource" ("tracks", "generated"). */
enum class QcMusicSource : quint8 {
    Tracks,    // The packs' tracks; composed pieces only where no track plays.
    Generated, // Composed pieces only, no tracks (for videos and streams).
};

/*! \brief The names of QcMusicSource in the settings file. */
namespace QcMusicSources {

/*! Returns the name of \a source in the settings file. */
[[nodiscard]] inline QString name(QcMusicSource source)
{
    return source == QcMusicSource::Generated ? QStringLiteral("generated") : QStringLiteral("tracks");
}

/*! Returns the source named \a text; none for an unknown name. */
[[nodiscard]] inline std::optional<QcMusicSource> fromName(const QString &text)
{
    for (const QcMusicSource source : {QcMusicSource::Tracks, QcMusicSource::Generated}) {
        if (text == name(source)) {
            return source;
        }
    }
    return std::nullopt;
}

} // namespace QcMusicSources
