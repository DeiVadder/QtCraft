// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/sound/QcAudioMixer.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace {

float clampVolume(float volume)
{
    return std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0.0f;
}

bool playable(const std::shared_ptr<const QcSampleBuffer> &buffer)
{
    return buffer && !buffer->samples.empty() && buffer->sampleRate > 0;
}

float finiteGain(float gain)
{
    return std::isfinite(gain) ? std::max(0.0f, gain) : 0.0f;
}

} // namespace

QcAudioMixer::QcAudioMixer(int outputRate)
    : m_outputRate(std::max(1, outputRate))
{
    m_categoryVolumes.fill(1.0f);
}

QcAudioMixer QcAudioMixer::withRate(int outputRate) const
{
    QcAudioMixer mixer(outputRate);
    for (const QcSoundCategory category : QcSoundCategories::all()) {
        mixer.setCategoryVolume(category, categoryVolume(category));
    }
    mixer.m_master = m_master;
    mixer.m_muted = m_muted;
    return mixer;
}

qsizetype QcAudioMixer::victimFor(float gain) const
{
    // The quietest one-shot effect; loops (ambience) and music keep playing, and a sound quieter
    // than every candidate is dropped instead.
    qsizetype victim = -1;
    for (qsizetype i = 0; i < m_voices.size(); ++i) {
        const Voice &candidate = m_voices[i];
        if (!candidate.loop && candidate.category != QcSoundCategory::Music && (victim < 0 || candidate.gain < m_voices[victim].gain)) {
            victim = i;
        }
    }
    return victim >= 0 && gain >= m_voices[victim].gain ? victim : -1;
}

bool QcAudioMixer::wouldPlay(const std::shared_ptr<const QcSampleBuffer> &buffer, float gain) const
{
    return playable(buffer) && (m_voices.size() < maxVoices || victimFor(finiteGain(gain)) >= 0);
}

QcAudioMixer::VoiceId QcAudioMixer::play(std::shared_ptr<const QcSampleBuffer> buffer, const PlayParameters &parameters)
{
    if (!playable(buffer)) {
        return 0;
    }

    Voice voice;
    voice.buffer = std::move(buffer);
    voice.loop = parameters.loop;
    return start(std::move(voice), parameters);
}

QcAudioMixer::VoiceId QcAudioMixer::playStream(std::shared_ptr<QcStreamCursor> stream, const PlayParameters &parameters)
{
    if (!stream || stream->sampleRate() <= 0) {
        return 0;
    }

    Voice voice;
    voice.stream = std::move(stream);
    return start(std::move(voice), parameters);
}

QcAudioMixer::VoiceId QcAudioMixer::start(Voice voice, const PlayParameters &parameters)
{
    const float gain = finiteGain(parameters.gain);
    if (m_voices.size() >= maxVoices) {
        const qsizetype victim = victimFor(gain);
        if (victim < 0) {
            return 0;
        }
        m_voices.removeAt(victim);
    }

    voice.id = m_nextId++;
    voice.pitch = std::isfinite(parameters.pitch) ? std::clamp(parameters.pitch, 0.1, 10.0) : 1.0;
    voice.gain = voice.appliedGain = gain;
    voice.pan = voice.appliedPan = std::isfinite(parameters.pan) ? std::clamp(parameters.pan, -1.0f, 1.0f) : 0.0f;
    voice.category = parameters.category;
    m_voices.append(std::move(voice));
    return m_voices.constLast().id;
}

void QcAudioMixer::stop(VoiceId id)
{
    m_voices.removeIf([id](const Voice &voice) { return voice.id == id; });
}

void QcAudioMixer::stopAllExcept(VoiceId kept)
{
    m_voices.removeIf([kept](const Voice &voice) { return voice.id != kept; });
}

void QcAudioMixer::fadeOut(VoiceId id, int milliseconds)
{
    const int frames = std::max(1, static_cast<int>(static_cast<qint64>(m_outputRate) * std::max(0, milliseconds) / 1000));
    for (Voice &voice : m_voices) {
        if (voice.id == id && voice.fadeFrames < 0) {
            voice.fadeFrames = frames;
            voice.gain = 0.0f;
            // It fades where it is heard now.
            voice.pan = voice.appliedPan;
        }
    }
}

void QcAudioMixer::setGain(VoiceId id, float gain)
{
    for (Voice &voice : m_voices) {
        if (voice.id == id && voice.fadeFrames < 0) {
            voice.gain = finiteGain(gain);
        }
    }
}

void QcAudioMixer::setPan(VoiceId id, float pan)
{
    for (Voice &voice : m_voices) {
        if (voice.id == id && voice.fadeFrames < 0) {
            voice.pan = std::isfinite(pan) ? std::clamp(pan, -1.0f, 1.0f) : 0.0f;
        }
    }
}

bool QcAudioMixer::isPlaying(VoiceId id) const
{
    return std::ranges::any_of(m_voices, [id](const Voice &voice) { return voice.id == id; });
}

void QcAudioMixer::setCategoryVolume(QcSoundCategory category, float volume)
{
    if (static_cast<std::size_t>(category) < m_categoryVolumes.size()) {
        m_categoryVolumes[static_cast<std::size_t>(category)] = clampVolume(volume);
    }
}

void QcAudioMixer::setMasterVolume(float volume)
{
    m_master = clampVolume(volume);
}

std::pair<float, float> QcAudioMixer::panGains(float pan)
{
    const double angle = (static_cast<double>(std::clamp(pan, -1.0f, 1.0f)) + 1.0) * std::numbers::pi / 4.0;
    return {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
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

namespace {

// One source sample (a buffer's: mono on both sides) or frame (a stream's) at the voice's
// position; none at its end.
std::optional<std::array<float, 2>> sourceFrame(QcAudioMixer::Voice &voice)
{
    if (voice.stream) {
        return voice.stream->frameAt(voice.position);
    }

    const std::vector<float> &samples = voice.buffer->samples;
    const auto length = static_cast<double>(samples.size());
    if (voice.position >= length && !voice.loop) {
        return std::nullopt;
    }
    if (voice.position >= length) {
        voice.position = std::fmod(voice.position, length);
    }

    const auto index = static_cast<std::size_t>(voice.position);
    const double fraction = voice.position - static_cast<double>(index);
    // Linear interpolation; a loop's last sample leads into its first.
    const std::size_t next = index + 1 < samples.size() ? index + 1 : (voice.loop ? 0 : index);
    const auto sample = static_cast<float>(samples[index] + (samples[next] - samples[index]) * fraction);
    return std::array<float, 2>{sample, sample};
}

} // namespace

bool QcAudioMixer::finished(const Voice &voice)
{
    if (voice.fadeFrames == 0) {
        return true;
    }
    if (voice.stream) {
        return voice.stream->ended();
    }
    return !voice.loop && voice.position >= static_cast<double>(voice.buffer->samples.size());
}

void QcAudioMixer::mixVoice(Voice &voice, std::span<float> out, double outputRate, float level)
{
    const int sourceRate = voice.stream ? voice.stream->sampleRate() : voice.buffer->sampleRate;
    const double step = voice.pitch * sourceRate / outputRate;
    const auto frames = static_cast<int>(out.size() / 2);
    // A fading voice reaches silence at the end of its fade, else gain and stereo position reach
    // their targets at the end of this block.
    const int rampFrames = voice.fadeFrames >= 0 ? std::min(frames, voice.fadeFrames) : frames;
    const int targetFrames = voice.fadeFrames >= 0 ? std::max(1, voice.fadeFrames) : std::max(1, frames);
    const float gainStep = (voice.gain - voice.appliedGain) / static_cast<float>(targetFrames);
    const auto [leftFrom, rightFrom] = panGains(voice.appliedPan);
    const auto [leftTo, rightTo] = panGains(voice.pan);
    const float leftStep = (leftTo - leftFrom) / static_cast<float>(targetFrames);
    const float rightStep = (rightTo - rightFrom) / static_cast<float>(targetFrames);
    // A stream is stereo already: its sides keep their level in the centre (the pan gains are
    // 1/sqrt(2) there).
    const float sides = voice.stream ? std::numbers::sqrt2_v<float> : 1.0f;
    float gain = voice.appliedGain;
    float left = leftFrom;
    float right = rightFrom;

    int frame = 0;
    for (; frame < rampFrames; ++frame) {
        const std::optional<std::array<float, 2>> source = sourceFrame(voice);
        if (!source) {
            break;
        }

        gain += gainStep;
        left += leftStep;
        right += rightStep;
        const float value = gain * level * sides;
        out[2 * static_cast<std::size_t>(frame)] += (*source)[0] * value * left;
        out[2 * static_cast<std::size_t>(frame) + 1] += (*source)[1] * value * right;
        voice.position += step;
    }

    // A fade can last longer than the block; it goes on from the gain reached.
    voice.appliedGain = voice.fadeFrames >= 0 ? gain : voice.gain;
    voice.appliedPan = voice.pan;
    if (voice.fadeFrames >= 0) {
        voice.fadeFrames -= rampFrames;
    }
}

std::span<const float> QcAudioMixer::mix(int frames)
{
    if (frames <= 0) {
        return {};
    }

    m_output.assign(static_cast<std::size_t>(frames) * 2, 0.0f);
    const float master = m_muted ? 0.0f : m_master;
    for (Voice &voice : m_voices) {
        mixVoice(voice, m_output, m_outputRate, categoryVolume(voice.category) * master);
    }

    m_voices.removeIf([](const Voice &voice) { return finished(voice); });
    for (float &sample : m_output) {
        sample = limit(sample);
    }
    return m_output;
}
