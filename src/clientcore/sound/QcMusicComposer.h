// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcMusicArrangement.h"
#include "clientcore/sound/QcSampleBuffer.h"
#include "common/content/registry/QcSoundDefinition.h"
#include "common/world/QcAtmosphere.h"

#include <QList>

#include <array>
#include <atomic>

/*! \brief Background music composed from a seed (decisions E-1123, E-1156, E-1157): slow held
    chords over a bass, changing every few bars along a progression drawn from general tendencies
    of tonal harmony, with a sparse melody of varied motifs (A A' B A) and, for some places, a soft
    plucked broken chord; it swells to a loudest point and fades out on the tonic, in a reverb.

    A style (QcMusicStyle, a biome's music) bounds the scale, the root and the tempo, picks the
    melody's sound and may hold a drone under the piece; an arrangement (QcMusicArrangement, the
    place's layout) sets length, harmonic rhythm, density, register, dynamic arc and reverb. */
class QcMusicComposer
{
public:
    /*! The instrument a note is played with. */
    enum class Voice {
        Lead,  // The melody
        Bass,  // The bass under each chord
        Pad,   // The soft held chords
        Drone, // A low root held under the whole piece (QcMusicStyle::drone)
        Keys   // A plucked, piano-like broken chord (QcMusicArrangement::keysNotesPerBar)
    };

    /*! \brief One note of a piece. */
    struct Note
    {
        /*! Start in seconds from the beginning of the piece. */
        double start = 0.0;
        /*! Length in seconds. */
        double duration = 0.0;
        /*! MIDI note number (69 = A4, 440 Hz). */
        int pitch = 69;
        /*! Loudness (0..1), the dynamic arc included. */
        float velocity = 0.7f;
        /*! Instrument. */
        Voice voice = Voice::Lead;

        friend bool operator==(const Note &, const Note &) = default;
    };

    /*! \brief A composed piece. */
    struct Piece
    {
        /*! The notes of all voices, voice by voice; the drone (if any) comes last. */
        QList<Note> notes;
        /*! Length of the piece in seconds: the music and the reverb's tail after it. */
        double seconds = 0.0;
        /*! Tempo. */
        double beatsPerMinute = 80.0;
        /*! MIDI note of the scale's root (bass register) and whether the scale is minor.
            \qcshared */
        int root = 45;
        bool minor = false;
        /*! The melody's wave and the cut-off of its low-pass (QcMusicStyle). \qcshared */
        QcSoundWave lead = QcSoundWave::Triangle;
        double leadLowpass = 3000.0;
        /*! The held chords' low-pass, the reverb time and the reverb's share
            (QcMusicArrangement). \qcshared */
        double padLowpass = 900.0;
        double reverbSeconds = 3.0;
        double reverbMix = 0.3;
    };

    /*! Beats per bar. */
    static constexpr int beatsPerBar = 4;
    /*! The longest reverb tail after the music, in seconds. */
    static constexpr double maxTailSeconds = 4.0;

    /*! Returns the semitones above the root of the major or, with \a minor, the natural minor
        scale. */
    [[nodiscard]] static std::array<int, 7> scale(bool minor);
    /*! Returns the frequency in Hz of MIDI note \a midiPitch (equal temperament, 69 = 440 Hz). */
    [[nodiscard]] static double frequency(int midiPitch);
    /*! Returns the piece composed from \a seed in \a style laid out by \a arrangement; the same
        seed, style and arrangement give the same piece. */
    [[nodiscard]] static Piece compose(quint64 seed, const QcMusicStyle &style = {}, const QcMusicArrangement &arrangement = {});
    /*! Returns the seconds of reverb tail after the music of \a piece. */
    [[nodiscard]] static double tailSeconds(const Piece &piece);
    /*! Returns \a piece rendered at \a sampleRate, its reverb included, normalised like every
        synthesized sound (QcSoundSynthesizer::peak). When \a stop is set (another thread may set
        it at any time; it is checked before each note), rendering stops and the buffer is
        empty. */
    [[nodiscard]] static QcSampleBuffer render(const Piece &piece, int sampleRate, const std::atomic<bool> *stop = nullptr);

private:
    static QcSoundLayer layerOf(const Note &note, const Piece &piece);
};
