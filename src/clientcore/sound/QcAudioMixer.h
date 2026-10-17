#pragma once

#include "clientcore/sound/QcSampleBuffer.h"
#include "common/content/definitions/QcSoundDefinition.h"

#include <QList>

#include <array>
#include <memory>
#include <utility>

// Software mixer: plays sample buffers as voices with their own gain, stereo position and speed,
// grouped into buses with a volume each, under a master volume and a mute switch. Produces
// interleaved stereo float frames. Not thread-safe; the audio output guards it with a lock.
class QcAudioMixer
{
public:
    using VoiceId = quint64;

    // When all voices are busy, a new sound replaces the quietest one that does not loop.
    static constexpr int maxVoices = 32;
    // Above this level the limiter bends the signal smoothly towards (but never reaching) 1.
    static constexpr float limiterKnee = 0.75f;

    struct Voice
    {
        VoiceId id = 0;
        std::shared_ptr<const QcSampleBuffer> buffer;
        // Read position in source samples; advances by `pitch` scaled to the output rate.
        double position = 0.0;
        double pitch = 1.0;
        float gain = 1.0f;
        // Gain actually reached; moves to `gain` over one mix() call so changes do not click.
        float appliedGain = 1.0f;
        float pan = 0.0f;
        QcSoundBus bus = QcSoundBus::Effects;
        bool loop = false;
    };

    struct PlayParameters
    {
        float gain = 1.0f;
        // -1 left, 0 centre, 1 right.
        float pan = 0.0f;
        double pitch = 1.0;
        QcSoundBus bus = QcSoundBus::Effects;
        bool loop = false;
    };

    explicit QcAudioMixer(int outputRate = 44100);

    int outputRate() const { return m_outputRate; }
    // Starts a voice; 0 when the buffer is empty or every voice loops and none can be replaced.
    VoiceId play(std::shared_ptr<const QcSampleBuffer> buffer, const PlayParameters &parameters);
    void stop(VoiceId id);
    void stopAll() { m_voices.clear(); }
    void setGain(VoiceId id, float gain);
    bool isPlaying(VoiceId id) const;
    int voiceCount() const { return int(m_voices.size()); }
    const QList<Voice> &voices() const { return m_voices; }

    void setBusVolume(QcSoundBus bus, float volume);
    float busVolume(QcSoundBus bus) const { return m_busVolumes[std::size_t(bus)]; }
    void setMasterVolume(float volume);
    float masterVolume() const { return m_master; }
    void setMuted(bool muted) { m_muted = muted; }
    bool isMuted() const { return m_muted; }

    // Overwrites `frames` interleaved stereo frames (left, right) of `out` with the mix and
    // advances all voices; voices that ran out are removed.
    void mix(float *out, int frames);

    // Left and right gain of a stereo position (constant power: both 1/sqrt(2) in the centre).
    static std::pair<float, float> panGains(float pan);
    // Soft limiter: unchanged up to limiterKnee, then bent so that |result| < 1 for any input.
    static float limit(float sample);

private:
    int m_outputRate = 44100;
    QList<Voice> m_voices;
    VoiceId m_nextId = 1;
    std::array<float, qcSoundBusCount> m_busVolumes{1.0f, 1.0f, 1.0f};
    float m_master = 1.0f;
    bool m_muted = false;
};
