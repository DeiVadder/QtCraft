// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/sound/QcPositionalVoices.h"

#include "common/content/registry/QcSoundKeys.h"

#include <algorithm>
#include <cmath>
#include <utility>

std::optional<QcAudioMixer::VoiceId> QcPositionalVoices::makeRoom(const QString &key)
{
    const auto sameKey = [&key](const Tracked &voice) { return voice.key == key; };
    if (std::ranges::count_if(m_voices, sameKey) < maxPerKey) {
        return std::nullopt;
    }

    // The list is in start order: the first of that key is the oldest.
    const auto oldest = std::ranges::find_if(m_voices, sameKey);
    const QcAudioMixer::VoiceId id = oldest->id;
    m_voices.erase(oldest);
    return id;
}

int QcPositionalVoices::playing(const QcAudioMixer &mixer, const QString &key) const
{
    return static_cast<int>(std::ranges::count_if(m_voices, [&](const Tracked &voice) { return voice.key == key && mixer.isPlaying(voice.id); }));
}

float QcPositionalVoices::stackedGain(int playing)
{
    return 1.0f / std::sqrt(static_cast<float>(std::max(0, playing) + 1));
}

QcAudioMixer::VoiceId QcPositionalVoices::start(QcAudioMixer &mixer, std::shared_ptr<const QcSampleBuffer> buffer, const QString &key,
                                                const QcSoundSpatial::Source &source, const QcAudioMixer::PlayParameters &parameters)
{
    QcSoundSpatial::Source heard = source;
    QcAudioMixer::PlayParameters played = parameters;
    if (key == QLatin1StringView(QcSoundKeys::explosion)) {
        // Kept in the source as well, so that update() does not bring the full level back.
        const float stacked = stackedGain(playing(mixer, key));
        heard.volume *= stacked;
        played.gain *= stacked;
    }

    // A sound the mixer would refuse does not push out one of the same key.
    if (!mixer.wouldPlay(buffer, played.gain)) {
        return 0;
    }

    // The oldest of the key gives way, briefly fading instead of cutting off.
    if (const std::optional<QcAudioMixer::VoiceId> oldest = makeRoom(key)) {
        mixer.fadeOut(*oldest);
    }
    const QcAudioMixer::VoiceId id = mixer.play(std::move(buffer), played);
    track(id, key, heard);
    return id;
}

void QcPositionalVoices::track(QcAudioMixer::VoiceId id, const QString &key, const QcSoundSpatial::Source &source)
{
    if (id == 0) {
        return;
    }

    if (m_voices.size() >= maxTracked) {
        m_voices.removeFirst();
    }
    m_voices.append({id, key, source});
}

void QcPositionalVoices::update(QcAudioMixer &mixer, const QcSoundSpatial::Listener &listener)
{
    m_voices.removeIf([&mixer](const Tracked &voice) { return !mixer.isPlaying(voice.id); });
    for (const Tracked &voice : std::as_const(m_voices)) {
        const QcSoundSpatial::Placement placement = QcSoundSpatial::hear(voice.source, listener);
        mixer.setGain(voice.id, placement.gain);
        mixer.setPan(voice.id, placement.pan);
    }
}
