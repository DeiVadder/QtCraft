#include "app/controllers/QcAudioSettings.h"

#include "app/debug/QcAppLogging.h"
#include "audio/QcAudioEngine.h"

QcAudioSettings::QcAudioSettings(QcAudioEngine *audio, QObject *parent)
    : QObject(parent)
    , m_audio(audio)
{
}

double QcAudioSettings::checkedVolume(double requested, double current)
{
    const double checked = QcGameSettings::clampVolume(requested, current);
    // The sliders stay inside 0..1; anything else came from outside and is corrected.
    if (checked != requested) {
        qCWarning(lcGame) << "Volume" << requested << "is not in 0..1: using" << checked;
    }
    return checked;
}

void QcAudioSettings::setMasterVolume(double volume)
{
    const double checked = checkedVolume(volume, m_masterVolume);
    if (m_masterVolume != checked) {
        m_masterVolume = checked;
        apply();
        emit changed();
    }
}

void QcAudioSettings::setEffectsVolume(double volume)
{
    const double checked = checkedVolume(volume, m_effectsVolume);
    if (m_effectsVolume != checked) {
        m_effectsVolume = checked;
        apply();
        emit changed();
    }
}

void QcAudioSettings::setMusicVolume(double volume)
{
    const double checked = checkedVolume(volume, m_musicVolume);
    if (m_musicVolume != checked) {
        m_musicVolume = checked;
        apply();
        emit changed();
    }
}

void QcAudioSettings::setAmbientVolume(double volume)
{
    const double checked = checkedVolume(volume, m_ambientVolume);
    if (m_ambientVolume != checked) {
        m_ambientVolume = checked;
        apply();
        emit changed();
    }
}

void QcAudioSettings::setMuted(bool muted)
{
    if (m_muted != muted) {
        m_muted = muted;
        apply();
        emit changed();
    }
}

void QcAudioSettings::load(const QcGameSettings::AudioVolumes &volumes)
{
    m_masterVolume = volumes.master;
    m_effectsVolume = volumes.effects;
    m_musicVolume = volumes.music;
    m_ambientVolume = volumes.ambient;
    m_muted = volumes.muted;
    apply();
    emit changed();
}

QcGameSettings::AudioVolumes QcAudioSettings::volumes() const
{
    return {m_masterVolume, m_effectsVolume, m_musicVolume, m_ambientVolume, m_muted};
}

void QcAudioSettings::apply() const
{
    m_audio->setVolumes(m_masterVolume, m_effectsVolume, m_musicVolume, m_ambientVolume, m_muted);
}
