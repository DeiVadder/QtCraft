// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcSampleBuffer.h"
#include "common/content/definitions/QcSoundDefinition.h"

#include <QList>

#include <array>
#include <memory>
#include <span>
#include <utility>
#include <vector>

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
    // A voice stopped with fadeOut() falls silent over this time instead of cutting off (a click).
    static constexpr int fadeOutMilliseconds = 5;

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
        // Stereo position actually reached; moves to `pan` over one mix() call like the gain.
        float appliedPan = 0.0f;
        // Frames left until a voice stopped with fadeOut() is silent and removed; -1 while it plays on.
        int fadeFrames = -1;
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
    // A mixer at `outputRate` without voices, with this one's bus and master volumes and mute (for
    // an output device that runs at another rate).
    [[nodiscard]] QcAudioMixer withRate(int outputRate) const;
    // Starts a voice; 0 when the buffer is empty or every voice loops and none can be replaced.
    VoiceId play(std::shared_ptr<const QcSampleBuffer> buffer, const PlayParameters &parameters);
    // Whether play() would start a voice for `buffer` at `gain` now.
    [[nodiscard]] bool wouldPlay(const std::shared_ptr<const QcSampleBuffer> &buffer, float gain) const;
    void stop(VoiceId id);
    // Lets the voice fall silent over fadeOutMilliseconds, then removes it; it keeps its place
    // until then and ignores setGain.
    void fadeOut(VoiceId id);
    void stopAll() { m_voices.clear(); }
    void setGain(VoiceId id, float gain);
    void setPan(VoiceId id, float pan);
    bool isPlaying(VoiceId id) const;
    int voiceCount() const { return static_cast<int>(m_voices.size()); }
    const QList<Voice> &voices() const { return m_voices; }

    void setBusVolume(QcSoundBus bus, float volume);
    float busVolume(QcSoundBus bus) const { return m_busVolumes[static_cast<std::size_t>(bus)]; }
    void setMasterVolume(float volume);
    float masterVolume() const { return m_master; }
    void setMuted(bool muted) { m_muted = muted; }
    bool isMuted() const { return m_muted; }

    // Mixes the next `frames` interleaved stereo frames (left, right) into the mixer's own buffer
    // (it keeps its capacity, so mixing allocates nothing once it has grown) and advances all voices;
    // voices that ran out are removed. The view stays valid until the next call.
    [[nodiscard]] std::span<const float> mix(int frames);

    // Left and right gain of a stereo position (constant power: both 1/sqrt(2) in the centre).
    static std::pair<float, float> panGains(float pan);
    // Soft limiter: unchanged up to limiterKnee, then bent so that |result| < 1 for any input.
    static float limit(float sample);

private:
    // When every voice is busy: the voice a new sound at `gain` replaces; -1 when it is refused.
    [[nodiscard]] qsizetype victimFor(float gain) const;
    // Adds `voice` to `out` (`frames` interleaved stereo frames) at the bus and master volume
    // `level`, and advances it.
    static void mixVoice(Voice &voice, std::span<float> out, double outputRate, float level);

    int m_outputRate = 44100;
    QList<Voice> m_voices;
    std::vector<float> m_output;
    VoiceId m_nextId = 1;
    std::array<float, qcSoundBusCount> m_busVolumes{1.0f, 1.0f, 1.0f};
    float m_master = 1.0f;
    bool m_muted = false;
};
