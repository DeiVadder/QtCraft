#pragma once

#include "clientcore/settings/QcGameSettings.h"

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

class QcAudioEngine;

// Volumes in 0..1 and the mute switch (GameController.settings.audio), applied to the audio engine
// as soon as they change.
class QcAudioSettings : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(double masterVolume READ masterVolume WRITE setMasterVolume NOTIFY changed FINAL)
    Q_PROPERTY(double effectsVolume READ effectsVolume WRITE setEffectsVolume NOTIFY changed FINAL)
    Q_PROPERTY(double musicVolume READ musicVolume WRITE setMusicVolume NOTIFY changed FINAL)
    Q_PROPERTY(double ambientVolume READ ambientVolume WRITE setAmbientVolume NOTIFY changed FINAL)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY changed FINAL)

public:
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
    bool m_worldPaused = false;
};
