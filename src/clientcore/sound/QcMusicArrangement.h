// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcAtmosphere.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/*! \brief How a composed piece is laid out for one music place (decision E-1156): its length, how
    often the chord changes, how dense and how high the voices play, its loudness over time and
    its reverb. The values follow the general traits measured from the tracks of that place
    (\c {scripts/music-traits.py}, \c {docs/Musik-Merkmale.md}); no melody, motif or chord
    sequence of a track is taken over.

    The client picks it from where the player is (QcMusicDirector::Situation::places); it is not
    sent by the server, the biome's QcMusicStyle still comes with its atmosphere. */
struct QcMusicArrangement
{
    /*! The longest piece in seconds: a composed piece is rendered whole into memory (about 58 MB
        at 48 kHz), unlike the streamed tracks. */
    static constexpr double longestPiece = 300.0;

    /*! The place it is made for: a slot of QcMusicLibrary (\c {"day"},
        \c {"biome:qtcraft:nether_wastes"}, \c {"dimension:qtcraft:nether"}). */
    QString place{};
    /*! Scale, root, tempo and lead of the place when the biome has no music of its own (the
        overworld and the menu); none keeps the default style. */
    std::optional<QcMusicStyle> style = std::nullopt;
    /*! Shortest and longest length in seconds, drawn per piece. \qcshared */
    double shortestSeconds = 200.0;
    double longestSeconds = 300.0;
    /*! Bars each chord holds: the harmonic rhythm. */
    int barsPerChord = 2;
    /*! Melody notes per bar while the melody plays. */
    double leadNotesPerBar = 1.5;
    /*! Share of the phrases (two chords each) between intro and outro in which the melody plays. */
    double leadShare = 0.6;
    /*! Octaves of the melody's lowest note above the root (bass register). */
    int leadOctaves = 1;
    /*! Notes per bar of a soft, plucked broken chord (piano-like); 0 plays none. */
    double keysNotesPerBar = 0.0;
    /*! Cut-off of the held chords' low-pass in Hz (their brightness). */
    double padLowpass = 900.0;
    /*! Reverb time (to -60 dB) in seconds and the share of reverb in the mix. \qcshared */
    double reverbSeconds = 3.0;
    double reverbMix = 0.3;
    /*! Where the loudest part lies, as a share of the piece. */
    double peakAt = 0.55;
    /*! Seconds at the start with only the held chords swelling in, and of the ending's fade.
        \qcshared */
    double fadeInSeconds = 6.0;
    double fadeOutSeconds = 12.0;

    /*! Returns the style a piece here uses: the biome's music \a biome when it has one (even if
        it equals the default style), else this place's own style, else the default style. */
    [[nodiscard]] QcMusicStyle styleFor(const std::optional<QcMusicStyle> &biome) const;

    /*! Returns the arrangements of the base game's places: menu, day, night, cave, the nether's
        biomes and the nether as a whole. */
    [[nodiscard]] static const QList<QcMusicArrangement> &all();
    /*! Returns the arrangement of the first of \a places that has one (the most specific comes
        first, as in QcMusicDirector::Situation); the day's when none has. */
    [[nodiscard]] static const QcMusicArrangement &forPlaces(const QStringList &places);
    /*! Returns the arrangement made for \a place, or null. */
    [[nodiscard]] static const QcMusicArrangement *find(const QString &place);

    friend bool operator==(const QcMusicArrangement &, const QcMusicArrangement &) = default;
};
