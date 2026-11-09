// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/music/QcMusicSource.h"
#include "clientcore/sound/QcSoundCategory.h"

class QSettings;

/*! \brief The volumes of the sound, whether it is muted and where the music comes from, stored as
    "audio/<key>" (decision E-1150).

    Each sound category is stored under its name ("audio/blocks"). Settings from before the
    categories had one effects volume ("audio/effects"): a category without its own key takes it,
    except weather, which takes the old ambience volume (rain was an ambience loop). Music and
    ambience keep their keys. Saving writes every category and, for an older version of the
    game, the blocks volume as "audio/effects". */
struct QcAudioVolumes
{
    /*! The master volume a fresh installation starts with. */
    static constexpr double defaultMasterVolume = 0.8;

    /*! Volume of everything. */
    double master = defaultMasterVolume;
    /*! Volume of each sound category. */
    QcSoundVolumes categories = defaultCategories();
    /*! Whether the sound is muted (the volumes stay). */
    bool muted = false;
    /*! Where the music comes from (decision E-1137); an unknown name in the file gives the
        tracks. */
    QcMusicSource musicSource = QcMusicSource::Tracks;

    /*! Returns whether all values equal those of the other. */
    bool operator==(const QcAudioVolumes &) const = default;

    /*! Returns the volume \a category starts with: music 0.5, ambience 0.7, the others 1. */
    [[nodiscard]] static double defaultVolume(QcSoundCategory category);
    /*! Returns every category at its defaultVolume(). */
    [[nodiscard]] static QcSoundVolumes defaultCategories();
    /*! Returns the values stored in \a file, with defaults for missing or broken ones and the old
        effects volume for categories stored before they existed. */
    [[nodiscard]] static QcAudioVolumes load(const QSettings &file);
    /*! Stores the values in \a file. */
    void save(QSettings &file) const;
};
