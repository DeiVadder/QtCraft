#pragma once

#include "audio/QcAudioOutput.h"
#include "clientcore/sound/QcAmbience.h"
#include "clientcore/sound/QcAudioMixer.h"
#include "clientcore/sound/QcSoundBank.h"
#include "clientcore/sound/QcSoundCue.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QFutureWatcher>
#include <QObject>
#include <QRandomGenerator>
#include <QSet>
#include <QTimer>

#include <array>
#include <memory>

// Everything the graphical client hears: sound cues placed around the listener, UI sounds, the
// four ambience loops and the background music. Lives in the GUI thread; rendering the sound bank
// and music pieces happens on worker threads, so no frame ever waits for synthesis.
class QcAudioEngine : public QObject
{
    Q_OBJECT

public:
    // Pause between music pieces, and before the first one, in seconds.
    static constexpr int minMusicPause = 60;
    static constexpr int maxMusicPause = 180;
    static constexpr int firstMusicDelay = 15;
    // Quieter voices are not worth a mixer voice.
    static constexpr float audibleGain = 0.002f;
    // Level of the music relative to its bus volume.
    static constexpr float musicGain = 0.6f;

    explicit QcAudioEngine(QObject *parent = nullptr);
    ~QcAudioEngine() override;

    // Opens the output device; with `enabled` false no device is touched and the game is silent.
    void start(bool enabled);
    bool isActive() const { return m_output.isActive(); }
    QcAudioOutput &output() { return m_output; }

    // Renders the content's sounds in the background; until that is done cues are skipped.
    void setContent(std::shared_ptr<const QcContentRegistry> content);
    const QcContentRegistry *content() const { return m_content.get(); }
    bool isBankReady() const { return m_bank != nullptr; }
    void setVolumes(double master, double effects, double music, double ambient, bool muted);

    // A cue heard by a listener with `ears` at that position looking along `yawDegrees`.
    void play(const QcSoundCue &cue, const QcVec3 &ears, double yawDegrees);
    // A sound without a position (menu clicks).
    void playUi(const QString &key);
    // Levels of the ambience loops; a loop starts with its first audible level.
    void setAmbience(QcAmbience::Levels levels);
    // Background music plays while enabled (in a world), with pauses between pieces.
    void setMusicEnabled(bool enabled);
    // Stops ambience, music and effects (leaving a world).
    void stopWorldSounds();

signals:
    void bankReady();

private:
    std::shared_ptr<const QcSampleBuffer> pickVariant(const QcSoundBank::Entry &entry);
    QcAudioMixer::VoiceId startVoice(const QcSoundBank::Entry &entry, const QcAudioMixer::PlayParameters &parameters);
    void scheduleMusic(int seconds);
    void renderMusic();
    void warnUnknown(const QString &key);

    QcAudioOutput m_output;
    std::shared_ptr<const QcContentRegistry> m_content;
    std::shared_ptr<const QcSoundBank> m_bank;
    QFutureWatcher<std::shared_ptr<const QcSoundBank>> m_bankWatcher;
    QFutureWatcher<std::shared_ptr<const QcSampleBuffer>> m_musicWatcher;
    QTimer m_musicTimer;
    bool m_musicEnabled = false;
    QcAudioMixer::VoiceId m_musicVoice = 0;
    std::array<QcAudioMixer::VoiceId, 4> m_ambienceVoices{};
    QcAmbience::Levels m_ambience;
    QRandomGenerator m_random;
    QSet<QString> m_warned;
};
