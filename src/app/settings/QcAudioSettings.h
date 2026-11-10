// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/settings/QcSoundVolumeModel.h"
#include "clientcore/settings/QcGameSettings.h"

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QcAudioEngine;
class QcMusicLibrary;

// Volumes in 0..1 (the master volume and one per sound category, decision E-1150), the mute switch
// and where the music comes from (GameController.settings.audio), applied to the audio engine as
// soon as they change.
class QcAudioSettings : public QObject
{
    Q_OBJECT
    // AudioSettings in QML, for its enumerations (AudioSettings.MusicSource.Generated).
    QML_NAMED_ELEMENT(AudioSettings)
    QML_UNCREATABLE("Provided by GameController.settings.audio")
    Q_PROPERTY(double masterVolume READ masterVolume WRITE setMasterVolume NOTIFY changed FINAL)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY changed FINAL)
    // The sliders of the sound categories, one row each in the order of SoundCategory.
    Q_PROPERTY(QcSoundVolumeModel *categories READ categories CONSTANT FINAL)
    // AudioSettings.MusicSource.Tracks (Scott Buckley's) or Generated (decision E-1137).
    Q_PROPERTY(QcAudioSettings::MusicSource musicSource READ musicSource WRITE setMusicSource NOTIFY changed FINAL)
    // The credit lines of the music tracks ("About", decision E-1138).
    Q_PROPERTY(QStringList musicCredits READ musicCredits NOTIFY musicCreditsChanged FINAL)

public:
    // QcMusicSource for QML, in its order.
    enum class MusicSource { Tracks, Generated };
    Q_ENUM(MusicSource)
    // QcSoundCategory for QML, in its order.
    enum class SoundCategory { Music, Weather, Blocks, Hostile, Friendly, Players, Ambient, Interface };
    Q_ENUM(SoundCategory)

    explicit QcAudioSettings(QcAudioEngine *audio, QObject *parent = nullptr);

    double masterVolume() const { return volumes().master; }
    void setMasterVolume(double volume);
    // The volume of `category`, 0..1.
    double volume(QcAudioSettings::SoundCategory category) const;
    // Sets the volume of `category` (an unknown category or a volume outside 0..1 is reported).
    Q_INVOKABLE void setVolume(QcAudioSettings::SoundCategory category, double volume);
    bool muted() const { return volumes().muted; }
    void setMuted(bool muted);
    QcSoundVolumeModel *categories() const { return m_categories; }
    MusicSource musicSource() const { return static_cast<MusicSource>(volumes().musicSource); }
    void setMusicSource(MusicSource source);
    [[nodiscard]] QStringList musicCredits() const;
    // Plays the tracks of `library` from now on (the base game's and the mods', read at start).
    void setMusicLibrary(std::shared_ptr<const QcMusicLibrary> library);

    // Takes the stored values and applies them.
    void load(const QcGameSettings::AudioVolumes &volumes);
    // The values to store.
    [[nodiscard]] const QcGameSettings::AudioVolumes &volumes() const { return m_volumes; }
    void apply() const;
    // The world pauses (decision E-877): music, weather and ambience fall silent until it resumes;
    // the other sounds (menu clicks) stay.
    void setWorldPaused(bool paused);
    bool worldPaused() const { return m_worldPaused; }
    // The language changed: the names of the categories follow.
    void retranslate();

signals:
    void changed();
    // The music library changed (not a setting: nothing is stored).
    void musicCreditsChanged();

private:
    // `requested` clamped to the sliders' range 0..1 (`current` for a value that is no number); a
    // value outside it came from outside and is corrected with a warning.
    [[nodiscard]] static double checkedVolume(double requested, double current);
    // Takes `volumes` as the new values: applies them and reports the change, if any.
    void setVolumes(const QcGameSettings::AudioVolumes &volumes);
    // The only place that writes the values.
    void store(const QcGameSettings::AudioVolumes &volumes) { m_volumes = volumes; }

    QcAudioEngine *m_audio = nullptr;
    QcSoundVolumeModel *m_categories = nullptr;
    QcGameSettings::AudioVolumes m_volumes;
    bool m_worldPaused = false;
};
