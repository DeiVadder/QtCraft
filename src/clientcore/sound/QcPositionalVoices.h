#pragma once

#include "clientcore/sound/QcAudioMixer.h"
#include "clientcore/sound/QcSoundSpatial.h"

#include <QList>
#include <QString>

#include <memory>
#include <optional>

// The mixer voices of positional sounds with where they sound, so that their loudness and stereo
// position follow the listener while they play (a thunder roll or an explosion lasts seconds, and
// the listener turns and walks meanwhile), and at most a few voices of one sound at a time, so
// that dozens of sources of the same sound (flowing water all around) never pile up. Not
// thread-safe; update() runs with the mixer locked.
class QcPositionalVoices
{
public:
    struct Tracked
    {
        QcAudioMixer::VoiceId id = 0;
        QString key;
        QcSoundSpatial::Source source;
    };

    // Never more than the mixer has voices; the oldest entry goes first.
    static constexpr int maxTracked = QcAudioMixer::maxVoices;
    // Voices of one sound key that play at once; a further one replaces the oldest.
    static constexpr int maxPerKey = 4;

    // Starts `buffer` in `mixer` as a voice of `key` heard from `source` and tracks it. When
    // maxPerKey voices of `key` play already, the oldest fades out (QcAudioMixer::fadeOut), unless
    // the mixer would refuse the new one anyway. A new explosion is quieter by stackedGain() of the
    // explosions still playing, so that a chain of blasts does not sit in the limiter for seconds
    // (decision E-879). Returns the voice, 0 when the mixer refused it.
    QcAudioMixer::VoiceId start(QcAudioMixer &mixer, std::shared_ptr<const QcSampleBuffer> buffer, const QString &key,
                                const QcSoundSpatial::Source &source, const QcAudioMixer::PlayParameters &parameters);
    void track(QcAudioMixer::VoiceId id, const QString &key, const QcSoundSpatial::Source &source);
    // The gain of a new explosion voice while `playing` others still sound: 1/sqrt(playing + 1).
    [[nodiscard]] static float stackedGain(int playing);
    void clear() { m_voices.clear(); }
    const QList<Tracked> &voices() const { return m_voices; }

    // Places every tracked voice still playing in `mixer` anew for `listener`; forgets the others.
    void update(QcAudioMixer &mixer, const QcSoundSpatial::Listener &listener);

private:
    // The voices of `key` that `mixer` still plays.
    [[nodiscard]] int playing(const QcAudioMixer &mixer, const QString &key) const;
    // Before a voice of `key` starts: when maxPerKey of them play already, forgets the oldest and
    // returns it.
    [[nodiscard]] std::optional<QcAudioMixer::VoiceId> makeRoom(const QString &key);

    QList<Tracked> m_voices;
};
