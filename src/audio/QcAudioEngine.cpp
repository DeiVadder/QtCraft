#include "audio/QcAudioEngine.h"

#include "audio/QcAudioLogging.h"
#include "clientcore/sound/QcMusicComposer.h"
#include "clientcore/sound/QcSoundSpatial.h"
#include "clientcore/sound/QcSoundSynthesizer.h"
#include "common/content/definitions/QcSoundKeys.h"

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

const std::array<QString, 4> &ambienceKeys()
{
    static const std::array<QString, 4> keys = {QString::fromLatin1(QcSoundKeys::ambientWind),
                                                QString::fromLatin1(QcSoundKeys::ambientCave),
                                                QString::fromLatin1(QcSoundKeys::ambientWater),
                                                QString::fromLatin1(QcSoundKeys::ambientRain)};
    return keys;
}

} // namespace

QcAudioEngine::QcAudioEngine(QObject *parent)
    : QObject(parent)
    , m_random(QRandomGenerator::global()->generate())
{
    m_musicTimer.setSingleShot(true);
    connect(&m_musicTimer, &QTimer::timeout, this, &QcAudioEngine::renderMusic);

    connect(&m_bankWatcher, &QFutureWatcherBase::finished, this, [this] {
        m_bank = m_bankWatcher.result();
        for (const QString &name : m_bank->skipped()) {
            qCWarning(lcAudio) << "Sound left out (memory budget):" << name;
        }
        emit bankReady();
    });

    connect(&m_musicWatcher, &QFutureWatcherBase::finished, this, [this] {
        const std::shared_ptr<const QcSampleBuffer> piece = m_musicWatcher.result();
        if (!m_musicEnabled || !isActive() || !piece || piece->samples.empty()) {
            return;
        }

        m_output.withMixer([this, &piece](QcAudioMixer &mixer) {
            m_musicVoice = mixer.play(piece, {musicGain, 0.0f, 1.0, QcSoundBus::Music, false});
        });
        scheduleMusic(static_cast<int>(piece->seconds()) + m_random.bounded(minMusicPause, maxMusicPause + 1));
    });

    connect(&m_output, &QcAudioOutput::mixerReset, this, &QcAudioEngine::mixerReset);
}

QcAudioEngine::~QcAudioEngine()
{
    // The workers only hold copies of what they need; waiting keeps their results from outliving
    // the watchers.
    m_bankWatcher.waitForFinished();
    m_musicWatcher.waitForFinished();
    m_output.stop();
}

void QcAudioEngine::start(bool enabled)
{
    if (!enabled) {
        qCInfo(lcAudio) << "Audio is switched off";
        return;
    }

    // Without an output device the game plays silently; start() logged why.
    [[maybe_unused]] const bool started = m_output.start();
}

void QcAudioEngine::setContent(std::shared_ptr<const QcContentRegistry> content)
{
    if (content == m_content) {
        return;
    }

    stopWorldSounds();
    m_content = std::move(content);
    buildBank();
}

void QcAudioEngine::buildBank()
{
    m_bank.reset();
    if (!m_content || !isActive()) {
        return;
    }

    const int rate = m_output.sampleRate();
    m_bankWatcher.setFuture(QtConcurrent::run([content = m_content, rate] {
        return std::make_shared<const QcSoundBank>(QcSoundBank::build(*content, rate));
    }));
}

void QcAudioEngine::mixerReset()
{
    // The new mixer is silent: the voices are gone. Ambience loops start again with the next
    // setAmbience once the bank is ready, music with its next piece.
    m_ambienceVoices = {};
    m_musicVoice = 0;
    m_positional.clear();
    buildBank();
    if (m_musicEnabled) {
        scheduleMusic(firstMusicDelay);
    }
}

void QcAudioEngine::setVolumes(double master, double effects, double music, double ambient, bool muted)
{
    m_output.withMixer([=](QcAudioMixer &mixer) {
        mixer.setMasterVolume(static_cast<float>(master));
        mixer.setBusVolume(QcSoundBus::Effects, static_cast<float>(effects));
        mixer.setBusVolume(QcSoundBus::Music, static_cast<float>(music));
        mixer.setBusVolume(QcSoundBus::Ambient, static_cast<float>(ambient));
        mixer.setMuted(muted);
    });
}

std::shared_ptr<const QcSampleBuffer> QcAudioEngine::pickVariant(const QcSoundBank::Entry &entry)
{
    if (entry.variants.isEmpty()) {
        return {};
    }

    return entry.variants[m_random.bounded(static_cast<int>(entry.variants.size()))];
}

QcAudioMixer::VoiceId QcAudioEngine::startVoice(const QcSoundBank::Entry &entry, const QcAudioMixer::PlayParameters &parameters)
{
    QcAudioMixer::VoiceId id = 0;
    std::shared_ptr<const QcSampleBuffer> buffer = pickVariant(entry);
    m_output.withMixer([&](QcAudioMixer &mixer) { id = mixer.play(std::move(buffer), parameters); });
    return id;
}

void QcAudioEngine::warnUnknown(const QString &key)
{
    // Bounded, so a server sending ever new names cannot grow it or flood the log.
    constexpr qsizetype maxWarnings = 256;
    if (!m_warned.contains(key) && m_warned.size() < maxWarnings) {
        m_warned.insert(key);
        qCWarning(lcAudio) << "Unknown sound, playing nothing:" << key;
    }
}

void QcAudioEngine::play(const QcSoundCue &cue, const QcSoundSpatial::Listener &listener)
{
    if (!isActive() || !m_bank) {
        return;
    }

    const QcSoundBank::Entry *entry = m_bank->find(cue.key);
    if (!entry) {
        warnUnknown(cue.key);
        return;
    }

    const QcSoundDefinition &sound = entry->definition;
    const QcSoundSpatial::Source source = QcSoundSpatial::source(sound, cue);
    const QcSoundSpatial::Placement placement = QcSoundSpatial::hear(source, listener);
    if (placement.gain < audibleGain) {
        return;
    }

    const double pitch = cue.pitch * (1.0 + sound.pitchVariation * (2.0 * m_random.generateDouble() - 1.0));
    std::shared_ptr<const QcSampleBuffer> buffer = pickVariant(*entry);
    m_output.withMixer([&](QcAudioMixer &mixer) {
        m_positional.start(mixer, std::move(buffer), cue.key, source, {placement.gain, placement.pan, pitch, sound.bus, false});
    });
}

void QcAudioEngine::moveListener(const QcSoundSpatial::Listener &listener)
{
    m_listener = listener;
    if (!isActive()) {
        return;
    }

    m_output.withMixer([&](QcAudioMixer &mixer) { m_positional.update(mixer, listener); });
}

void QcAudioEngine::playUi(const QString &key)
{
    if (!isActive() || !m_bank) {
        return;
    }

    const QcSoundBank::Entry *entry = m_bank->find(key);
    if (!entry) {
        warnUnknown(key);
        return;
    }

    const double pitch = 1.0 + entry->definition.pitchVariation * (2.0 * m_random.generateDouble() - 1.0);
    startVoice(*entry, {static_cast<float>(entry->definition.volume), 0.0f, pitch, entry->definition.bus, false});
}

void QcAudioEngine::setAmbience(QcAmbience::Levels levels)
{
    m_ambience = levels;
    if (!isActive() || !m_bank) {
        return;
    }

    const std::array<float, 4> values = {levels.wind, levels.cave, levels.water, levels.rain};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const QcSoundBank::Entry *entry = m_bank->find(ambienceKeys()[i]);
        if (!entry) {
            continue;
        }

        const float gain = values[i] * static_cast<float>(entry->definition.volume);
        QcAudioMixer::VoiceId &voice = m_ambienceVoices[i];
        if (voice == 0) {
            // Levels fade in from zero, so the loop starts quietly.
            if (gain >= audibleGain) {
                voice = startVoice(*entry, {gain, 0.0f, 1.0, entry->definition.bus, true});
            }
        } else {
            m_output.withMixer([voice, gain](QcAudioMixer &mixer) { mixer.setGain(voice, gain); });
        }
    }
}

void QcAudioEngine::setMusicEnabled(bool enabled)
{
    if (m_musicEnabled == enabled) {
        return;
    }

    m_musicEnabled = enabled;
    if (enabled) {
        scheduleMusic(firstMusicDelay + m_random.bounded(firstMusicDelay + 1));
        return;
    }

    m_musicTimer.stop();
    const QcAudioMixer::VoiceId voice = m_musicVoice;
    m_output.withMixer([voice](QcAudioMixer &mixer) { mixer.stop(voice); });
    m_musicVoice = 0;
}

void QcAudioEngine::scheduleMusic(int seconds)
{
    m_musicTimer.start(std::max(1, seconds) * 1000);
}

void QcAudioEngine::renderMusic()
{
    if (!m_musicEnabled || !isActive()) {
        return;
    }

    const quint64 seed = m_random.generate64();
    const int rate = m_output.sampleRate();
    m_musicWatcher.setFuture(QtConcurrent::run([seed, rate] {
        return std::make_shared<const QcSampleBuffer>(QcMusicComposer::render(QcMusicComposer::compose(seed), rate));
    }));
}

void QcAudioEngine::stopWorldSounds()
{
    m_output.withMixer([](QcAudioMixer &mixer) { mixer.stopAll(); });
    m_ambienceVoices = {};
    m_musicVoice = 0;
    m_ambience = {};
    m_positional.clear();
}
