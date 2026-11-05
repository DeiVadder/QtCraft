// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcSampleBuffer.h"

#include <QList>

#include <array>

/*! \brief Background music composed from a seed: a calm piece on a pentatonic scale with a
    melody of varied motifs (A A' B A), a slow bass line and soft held chords, ending on the
    root. */
class QcMusicComposer
{
public:
    /*! The instrument a note is played with. */
    enum class Voice {
        Lead, // The melody
        Bass, // The slow bass line
        Pad   // The soft held chords
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
        /*! Loudness (0..1). */
        float velocity = 0.7f;
        /*! Instrument. */
        Voice voice = Voice::Lead;

        friend bool operator==(const Note &, const Note &) = default;
    };

    /*! \brief A composed piece. */
    struct Piece
    {
        /*! The notes of all voices. */
        QList<Note> notes;
        /*! Length of the piece in seconds, the last chord's ring included. */
        double seconds = 0.0;
        /*! Tempo. */
        double beatsPerMinute = 80.0;
        /*! MIDI note of the scale's root (bass register) and whether it is the minor pentatonic.
            \qcshared */
        int root = 45;
        bool minor = false;
    };

    /*! Beats per bar. */
    static constexpr int beatsPerBar = 4;
    /*! Bars per phrase of the melody. */
    static constexpr int barsPerPhrase = 2;

    /*! Returns the semitones above the root of the major (0 2 4 7 9) or, with \a minor, the minor
        (0 3 5 7 10) pentatonic scale. */
    static std::array<int, 5> scale(bool minor);
    /*! Returns the frequency in Hz of MIDI note \a midiPitch (equal temperament, 69 = 440 Hz). */
    static double frequency(int midiPitch);
    /*! Returns the piece composed from \a seed; the same seed gives the same piece. */
    static Piece compose(quint64 seed);
    /*! Returns \a piece rendered at \a sampleRate. */
    static QcSampleBuffer render(const Piece &piece, int sampleRate);
};
