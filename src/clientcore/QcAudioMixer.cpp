#include "QcAudioMixer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

float clampVolume(float volume)
{
    return std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0.0f;
}

} // namespace

QcAudioMixer::QcAudioMixer(int outputRate)
    : m_outputRate(std::max(1, outputRate))
{
}

QcAudioMixer::VoiceId QcAudioMixer::play(std::shared_ptr<const QcSampleBuffer> buffer, const PlayParameters &parameters)
{
    if (!buffer || buffer->samples.empty() || buffer->sampleRate <= 0) {
        return 0;
    }
    if (m_voices.size() >= maxVoices) {
        // Replace the quietest one-shot effect; loops (ambience) and music keep playing, and a
        // sound quieter than every candidate is dropped instead.
        qsizetype victim = -1;
        for (qsizetype i = 0; i < m_voices.size(); ++i) {
            const Voice &candidate = m_voices[i];
            if (!candidate.loop && candidate.bus != QcSoundBus::Music && (victim < 0 || candidate.gain < m_voices[victim].gain)) {
                victim = i;
            }
        }
        const float gain = std::isfinite(parameters.gain) ? parameters.gain : 0.0f;
        if (victim < 0 || gain < m_voices[victim].gain) {
            return 0;
        }
        m_voices.removeAt(victim);
    }
    Voice voice;
    voice.id = m_nextId++;
    voice.buffer = std::move(buffer);
    voice.pitch = std::isfinite(parameters.pitch) ? std::clamp(parameters.pitch, 0.1, 10.0) : 1.0;
    voice.gain = voice.appliedGain = std::isfinite(parameters.gain) ? std::max(0.0f, parameters.gain) : 0.0f;
    voice.pan = std::isfinite(parameters.pan) ? std::clamp(parameters.pan, -1.0f, 1.0f) : 0.0f;
    voice.bus = parameters.bus;
    voice.loop = parameters.loop;
    m_voices.append(std::move(voice));
    return m_voices.constLast().id;
}

void QcAudioMixer::stop(VoiceId id)
{
    m_voices.removeIf([id](const Voice &voice) { return voice.id == id; });
}

void QcAudioMixer::setGain(VoiceId id, float gain)
{
    for (Voice &voice : m_voices) {
        if (voice.id == id) {
            voice.gain = std::isfinite(gain) ? std::max(0.0f, gain) : 0.0f;
        }
    }
}

bool QcAudioMixer::isPlaying(VoiceId id) const
{
    return std::ranges::any_of(m_voices, [id](const Voice &voice) { return voice.id == id; });
}

void QcAudioMixer::setBusVolume(QcSoundBus bus, float volume)
{
    if (std::size_t(bus) < m_busVolumes.size()) {
        m_busVolumes[std::size_t(bus)] = clampVolume(volume);
    }
}

void QcAudioMixer::setMasterVolume(float volume)
{
    m_master = clampVolume(volume);
}

std::pair<float, float> QcAudioMixer::panGains(float pan)
{
    const double angle = (double(std::clamp(pan, -1.0f, 1.0f)) + 1.0) * std::numbers::pi / 4.0;
    return {float(std::cos(angle)), float(std::sin(angle))};
}

float QcAudioMixer::limit(float sample)
{
    if (!std::isfinite(sample)) {
        return 0.0f;
    }
    const float magnitude = std::abs(sample);
    if (magnitude <= limiterKnee) {
        return sample;
    }
    constexpr float headroom = 1.0f - limiterKnee;
    const float bent = limiterKnee + headroom * std::tanh((magnitude - limiterKnee) / headroom);
    // tanh reaches 1 in float precision for large inputs; stay strictly below full scale.
    return std::copysign(std::min(bent, 0.9999f), sample);
}

void QcAudioMixer::mix(float *out, int frames)
{
    if (frames <= 0) {
        return;
    }

    std::fill(out, out + qsizetype(frames) * 2, 0.0f);
    const float master = m_muted ? 0.0f : m_master;
    for (Voice &voice : m_voices) {
        const std::vector<float> &samples = voice.buffer->samples;
        const auto length = double(samples.size());
        const double step = voice.pitch * voice.buffer->sampleRate / m_outputRate;
        const float bus = m_busVolumes[std::size_t(voice.bus)] * master;
        const auto [left, right] = panGains(voice.pan);
        const float gainStep = (voice.gain - voice.appliedGain) / float(frames);
        float gain = voice.appliedGain;

        for (int frame = 0; frame < frames; ++frame) {
            if (voice.position >= length && !voice.loop) {
                break;
            }
            if (voice.position >= length) {
                voice.position = std::fmod(voice.position, length);
            }
            const auto index = std::size_t(voice.position);
            const double fraction = voice.position - double(index);
            // Linear interpolation; a loop's last sample leads into its first.
            const std::size_t next = index + 1 < samples.size() ? index + 1 : (voice.loop ? 0 : index);
            const auto sample = float(samples[index] + (samples[next] - samples[index]) * fraction);
            gain += gainStep;
            const float value = sample * gain * bus;
            out[2 * qsizetype(frame)] += value * left;
            out[2 * qsizetype(frame) + 1] += value * right;
            voice.position += step;
        }
        voice.appliedGain = voice.gain;
    }

    m_voices.removeIf([](const Voice &voice) { return !voice.loop && voice.position >= double(voice.buffer->samples.size()); });
    for (int i = 0; i < frames * 2; ++i) {
        out[i] = limit(out[i]);
    }
}
