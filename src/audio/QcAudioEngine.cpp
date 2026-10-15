#include "QcAudioEngine.h"

#include "QcMusicComposer.h"
#include "QcSoundKeys.h"
#include "QcSoundSpatial.h"
#include "QcSoundSynthesizer.h"

#include <QLoggingCategory>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcAudioEngine, "qtcraft.audio")

const std::array<QString, 3> &ambienceKeys()
{
    static const std::array<QString, 3> keys = {QString::fromLatin1(QcSoundKeys::ambientWind),
                                                QString::fromLatin1(QcSoundKeys::ambientCave),
                                                QString::fromLatin1(QcSoundKeys::ambientWater)};
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
            qCWarning(lcAudioEngine) << "Sound left out (memory budget):" << name;
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
        scheduleMusic(int(piece->seconds()) + m_random.bounded(minMusicPause, maxMusicPause + 1));
    });
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
        qCInfo(lcAudioEngine) << "Audio is switched off";
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
    m_bank.reset();
    if (!m_content || !isActive()) {
        return;
    }

    const int rate = m_output.sampleRate();
    m_bankWatcher.setFuture(QtConcurrent::run([content = m_content, rate] {
        return std::make_shared<const QcSoundBank>(QcSoundBank::build(*content, rate));
    }));
}

void QcAudioEngine::setVolumes(double master, double effects, double music, double ambient, bool muted)
{
    m_output.withMixer([=](QcAudioMixer &mixer) {
        mixer.setMasterVolume(float(master));
        mixer.setBusVolume(QcSoundBus::Effects, float(effects));
        mixer.setBusVolume(QcSoundBus::Music, float(music));
        mixer.setBusVolume(QcSoundBus::Ambient, float(ambient));
        mixer.setMuted(muted);
    });
}

std::shared_ptr<const QcSampleBuffer> QcAudioEngine::pickVariant(const QcSoundBank::Entry &entry)
{
    if (entry.variants.isEmpty()) {
        return {};
    }

    return entry.variants[m_random.bounded(int(entry.variants.size()))];
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
        qCWarning(lcAudioEngine) << "Unknown sound, playing nothing:" << key;
    }
}

void QcAudioEngine::play(const QcSoundCue &cue, const QcVec3 &ears, double yawDegrees)
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
    const QcSoundSpatial::Placement placement =
        QcSoundSpatial::place(ears, yawDegrees, cue.position, sound.range * std::max(1.0, double(cue.volume)));
    const float gain = float(sound.volume) * cue.volume * placement.gain;
    if (gain < audibleGain) {
        return;
    }

    const double pitch = cue.pitch * (1.0 + sound.pitchVariation * (2.0 * m_random.generateDouble() - 1.0));
    startVoice(*entry, {gain, placement.pan, pitch, sound.bus, false});
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
    startVoice(*entry, {float(entry->definition.volume), 0.0f, pitch, entry->definition.bus, false});
}

void QcAudioEngine::setAmbience(QcAmbience::Levels levels)
{
    m_ambience = levels;
    if (!isActive() || !m_bank) {
        return;
    }

    const std::array<float, 3> values = {levels.wind, levels.cave, levels.water};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const QcSoundBank::Entry *entry = m_bank->find(ambienceKeys()[i]);
        if (!entry) {
            continue;
        }

        const float gain = values[i] * float(entry->definition.volume);
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
}
