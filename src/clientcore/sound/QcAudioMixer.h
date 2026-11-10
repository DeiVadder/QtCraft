// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcSampleBuffer.h"
#include "clientcore/sound/QcSoundCategory.h"
#include "clientcore/sound/QcStreamCursor.h"

#include <QList>

#include <array>
#include <memory>
#include <span>
#include <utility>
#include <vector>

/*! \brief Software mixer: plays sample buffers as voices with their own gain, stereo position and
    speed, grouped into sound categories with a volume each (decision E-1150), under a master
    volume and a mute switch.

    Produces interleaved stereo float frames. Not thread-safe; the audio output guards it with a
    lock. */
class QcAudioMixer
{
public:
    /*! Id of a playing voice; 0 is none. */
    using VoiceId = quint64;

    /*! When all voices are busy, a new sound replaces the quietest one that does not loop. */
    static constexpr int maxVoices = 32;
    /*! Above this level the limiter bends the signal smoothly towards (but never reaching) 1. */
    static constexpr float limiterKnee = 0.75f;
    /*! A voice stopped with fadeOut() falls silent over this time instead of cutting off (a
        click). */
    static constexpr int fadeOutMilliseconds = 5;

    /*! \brief One playing sound. */
    struct Voice
    {
        /*! Id play() returned for it. */
        VoiceId id = 0;
        /*! The samples it plays; null for a stream. */
        std::shared_ptr<const QcSampleBuffer> buffer;
        /*! The stream it plays instead (a music track, decision E-1135; it never loops); null for
            a buffer. */
        std::shared_ptr<QcStreamCursor> stream;
        /*! Read position in source samples (frames of a stream); advances by \c {pitch} scaled to
            the output rate. */
        double position = 0.0;
        /*! Playback speed; 1 is the buffer's own. */
        double pitch = 1.0;
        /*! Gain asked for (at least 0). */
        float gain = 1.0f;
        /*! Gain actually reached; moves to \c {gain} over one mix() call so changes do not
            click. */
        float appliedGain = 1.0f;
        /*! Stereo position asked for: -1 left, 0 centre, 1 right. */
        float pan = 0.0f;
        /*! Stereo position actually reached; moves to \c {pan} over one mix() call like the
            gain. */
        float appliedPan = 0.0f;
        /*! Frames left until a voice stopped with fadeOut() is silent and removed; -1 while it
            plays on. */
        int fadeFrames = -1;
        /*! Category whose volume applies. */
        QcSoundCategory category = QcSoundCategory::Players;
        /*! Starts over at the end instead of ending. */
        bool loop = false;
    };

    /*! \brief How play() starts a voice. */
    struct PlayParameters
    {
        /*! Gain of the voice (at least 0). */
        float gain = 1.0f;
        /*! -1 left, 0 centre, 1 right. */
        float pan = 0.0f;
        /*! Playback speed; 1 is the buffer's own. */
        double pitch = 1.0;
        /*! Category whose volume applies. */
        QcSoundCategory category = QcSoundCategory::Players;
        /*! Starts over at the end instead of ending. */
        bool loop = false;
    };

    explicit QcAudioMixer(int outputRate = 44100);

    /*! Returns the output rate in frames per second. */
    int outputRate() const { return m_outputRate; }
    /*! Returns a mixer at \a outputRate without voices, with this one's category and master
        volumes and mute (for an output device that runs at another rate). */
    [[nodiscard]] QcAudioMixer withRate(int outputRate) const;
    /*! Starts a voice playing \a buffer with \a parameters and returns its id; 0 when the buffer
        is empty or every voice loops and none can be replaced. */
    VoiceId play(std::shared_ptr<const QcSampleBuffer> buffer, const PlayParameters &parameters);
    /*! Starts a voice playing \a stream (stereo, decoded while it plays; decision E-1135) with
        \a parameters (\c {loop} is ignored) and returns its id; 0 when \a stream is null or every
        voice loops and none can be replaced. */
    VoiceId playStream(std::shared_ptr<QcStreamCursor> stream, const PlayParameters &parameters);
    /*! Returns whether play() would start a voice for \a buffer at \a gain now. */
    [[nodiscard]] bool wouldPlay(const std::shared_ptr<const QcSampleBuffer> &buffer, float gain) const;
    /*! Stops the voice \a id at once. */
    void stop(VoiceId id);
    /*! Lets the voice \a id fall silent over \a milliseconds (at least one frame), then removes
        it; it keeps its place until then and ignores setGain. */
    void fadeOut(VoiceId id, int milliseconds = fadeOutMilliseconds);
    /*! Stops every voice at once. */
    void stopAll() { m_voices.clear(); }
    /*! Stops every voice except \a kept (0: none kept) at once. */
    void stopAllExcept(VoiceId kept);
    /*! Sets the gain of voice \a id to \a gain (at least 0; not finite: 0); ignored while it fades
        out. */
    void setGain(VoiceId id, float gain);
    /*! Sets the stereo position of voice \a id to \a pan (clamped to -1..1; not finite: 0); ignored
        while it fades out. */
    void setPan(VoiceId id, float pan);
    /*! Returns whether voice \a id is playing (also while it fades out). */
    bool isPlaying(VoiceId id) const;
    /*! Returns the number of voices. */
    int voiceCount() const { return static_cast<int>(m_voices.size()); }
    /*! Returns the voices. */
    const QList<Voice> &voices() const { return m_voices; }

    /*! Sets the volume of \a category to \a volume (clamped to 0..1; not finite: 0). */
    void setCategoryVolume(QcSoundCategory category, float volume);
    /*! Returns the volume of \a category. */
    float categoryVolume(QcSoundCategory category) const
    {
        Q_ASSERT(static_cast<std::size_t>(category) < m_categoryVolumes.size());
        return m_categoryVolumes[static_cast<std::size_t>(category)];
    }
    /*! Sets the master volume to \a volume (clamped to 0..1; not finite: 0). */
    void setMasterVolume(float volume);
    /*! Returns the master volume. */
    float masterVolume() const { return m_master; }
    /*! Mutes the output when \a muted, keeping every voice running. */
    void setMuted(bool muted) { m_muted = muted; }
    /*! Returns whether the output is muted. */
    bool isMuted() const { return m_muted; }

    /*! Returns the next \a frames interleaved stereo frames (left, right), mixed into the mixer's
        own buffer (it keeps its capacity, so mixing allocates nothing once it has grown) and
        advances all voices; voices that ran out are removed. The view stays valid until the next
        call. */
    [[nodiscard]] std::span<const float> mix(int frames);

    /*! Returns the left and right gain of the stereo position \a pan (constant power: both
        1/sqrt(2) in the centre). */
    static std::pair<float, float> panGains(float pan);
    /*! Returns \a sample through a soft limiter: unchanged up to limiterKnee, then bent so that
        |result| < 1 for any input. */
    static float limit(float sample);

private:
    // When every voice is busy: the voice a new sound at `gain` replaces; -1 when it is refused.
    [[nodiscard]] qsizetype victimFor(float gain) const;
    // Starts `voice` (with its source set) with `parameters`; its id, or 0 when no voice is free.
    VoiceId start(Voice voice, const PlayParameters &parameters);
    // Adds `voice` to `out` (`frames` interleaved stereo frames) at the category and master volume
    // `level`, and advances it.
    static void mixVoice(Voice &voice, std::span<float> out, double outputRate, float level);
    // Returns whether `voice` played to its end or faded out.
    [[nodiscard]] static bool finished(const Voice &voice);

    int m_outputRate = 44100;
    QList<Voice> m_voices;
    std::vector<float> m_output;
    VoiceId m_nextId = 1;
    // Read and written only through categoryVolume() and setCategoryVolume(); every category
    // starts at full volume (constructor).
    std::array<float, qcSoundCategoryCount> m_categoryVolumes{};
    float m_master = 1.0f;
    bool m_muted = false;
};
