#include "app/controllers/QcAudioSettings.h"

#include "app/debug/QcAppLogging.h"
#include "audio/QcAudioEngine.h"

QcAudioSettings::QcAudioSettings(QcAudioEngine *audio, QObject *parent)
    : QObject(parent)
    , m_audio(audio)
{
}

void QcAudioSettings::changeVolume(double &volume, double requested)
{
    const double asked = requested;
    requested = QcGameSettings::clampVolume(requested, volume);
    // The sliders stay inside 0..1; anything else came from outside and is corrected.
    if (asked != requested) {
        qCWarning(lcGame) << "Volume" << asked << "is not in 0..1: using" << requested;
    }
    if (volume != requested) {
        volume = requested;
        apply();
        emit changed();
    }
}

void QcAudioSettings::setMasterVolume(double volume)
{
    changeVolume(m_masterVolume, volume);
}

void QcAudioSettings::setEffectsVolume(double volume)
{
    changeVolume(m_effectsVolume, volume);
}

void QcAudioSettings::setMusicVolume(double volume)
{
    changeVolume(m_musicVolume, volume);
}

void QcAudioSettings::setAmbientVolume(double volume)
{
    changeVolume(m_ambientVolume, volume);
}

void QcAudioSettings::setMuted(bool muted)
{
    if (m_muted != muted) {
        m_muted = muted;
        apply();
        emit changed();
    }
}

void QcAudioSettings::load(const QcGameSettings &settings)
{
    m_masterVolume = settings.masterVolume;
    m_effectsVolume = settings.effectsVolume;
    m_musicVolume = settings.musicVolume;
    m_ambientVolume = settings.ambientVolume;
    m_muted = settings.muted;
    apply();
    emit changed();
}

void QcAudioSettings::store(QcGameSettings &settings) const
{
    settings.masterVolume = m_masterVolume;
    settings.effectsVolume = m_effectsVolume;
    settings.musicVolume = m_musicVolume;
    settings.ambientVolume = m_ambientVolume;
    settings.muted = m_muted;
}

void QcAudioSettings::apply() const
{
    m_audio->setVolumes(m_masterVolume, m_effectsVolume, m_musicVolume, m_ambientVolume, m_muted);
}
