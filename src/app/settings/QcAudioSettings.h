// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/settings/QcGameSettings.h"

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QcAudioEngine;
class QcMusicLibrary;

// Volumes in 0..1, the mute switch and where the music comes from (GameController.settings.audio),
// applied to the audio engine as soon as they change.
class QcAudioSettings : public QObject
{
    Q_OBJECT
    // AudioSettings in QML, for its enumeration (AudioSettings.MusicSource.Generated).
    QML_NAMED_ELEMENT(AudioSettings)
    QML_UNCREATABLE("Provided by GameController.settings.audio")
    Q_PROPERTY(double masterVolume READ masterVolume WRITE setMasterVolume NOTIFY changed FINAL)
    Q_PROPERTY(double effectsVolume READ effectsVolume WRITE setEffectsVolume NOTIFY changed FINAL)
    Q_PROPERTY(double musicVolume READ musicVolume WRITE setMusicVolume NOTIFY changed FINAL)
    Q_PROPERTY(double ambientVolume READ ambientVolume WRITE setAmbientVolume NOTIFY changed FINAL)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY changed FINAL)
    // AudioSettings.MusicSource.Tracks (Scott Buckley's) or Generated (decision E-1137).
    Q_PROPERTY(QcAudioSettings::MusicSource musicSource READ musicSource WRITE setMusicSource NOTIFY changed FINAL)
    // The credit lines of the music tracks ("About", decision E-1138).
    Q_PROPERTY(QStringList musicCredits READ musicCredits NOTIFY musicCreditsChanged FINAL)

public:
    // QcMusicSource for QML, in its order.
    enum class MusicSource { Tracks, Generated };
    Q_ENUM(MusicSource)

    explicit QcAudioSettings(QcAudioEngine *audio, QObject *parent = nullptr);

    double masterVolume() const { return m_masterVolume; }
    void setMasterVolume(double volume);
    double effectsVolume() const { return m_effectsVolume; }
    void setEffectsVolume(double volume);
    double musicVolume() const { return m_musicVolume; }
    void setMusicVolume(double volume);
    double ambientVolume() const { return m_ambientVolume; }
    void setAmbientVolume(double volume);
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    MusicSource musicSource() const { return static_cast<MusicSource>(m_musicSource); }
    void setMusicSource(MusicSource source);
    [[nodiscard]] QStringList musicCredits() const;
    // Plays the tracks of `library` from now on (the base game's and the mods', read at start).
    void setMusicLibrary(std::shared_ptr<const QcMusicLibrary> library);

    // Takes the stored values and applies them.
    void load(const QcGameSettings::AudioVolumes &volumes);
    // The values to store.
    [[nodiscard]] QcGameSettings::AudioVolumes volumes() const;
    void apply() const;
    // The world pauses (decision E-877): music and ambience fall silent until it resumes; the
    // effects (menu clicks) stay.
    void setWorldPaused(bool paused);
    bool worldPaused() const { return m_worldPaused; }

signals:
    void changed();
    // The music library changed (not a setting: nothing is stored).
    void musicCreditsChanged();

private:
    // `requested` clamped to the sliders' range 0..1 (`current` for a value that is no number); a
    // value outside it came from outside and is corrected with a warning.
    [[nodiscard]] static double checkedVolume(double requested, double current);

    QcAudioEngine *m_audio = nullptr;
    double m_masterVolume = QcGameSettings::defaultMasterVolume;
    double m_effectsVolume = QcGameSettings::defaultEffectsVolume;
    double m_musicVolume = QcGameSettings::defaultMusicVolume;
    double m_ambientVolume = QcGameSettings::defaultAmbientVolume;
    bool m_muted = false;
    QcMusicSource m_musicSource = QcMusicSource::Tracks;
    bool m_worldPaused = false;
};
