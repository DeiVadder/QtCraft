#pragma once

#include "clientcore/sound/QcSampleBuffer.h"

#include <QList>

#include <array>

// Background music composed from a seed: a calm piece on a pentatonic scale with a melody of
// varied motifs (A A' B A), a slow bass line and soft held chords, ending on the root.
class QcMusicComposer
{
public:
    enum class Voice { Lead, Bass, Pad };

    struct Note
    {
        double start = 0.0;
        double duration = 0.0;
        // MIDI note number (69 = A4, 440 Hz).
        int pitch = 69;
        float velocity = 0.7f;
        Voice voice = Voice::Lead;

        friend bool operator==(const Note &, const Note &) = default;
    };

    struct Piece
    {
        QList<Note> notes;
        double seconds = 0.0;
        double beatsPerMinute = 80.0;
        // MIDI note of the scale's root (bass register) and whether it is the minor pentatonic.
        int root = 45;
        bool minor = false;
    };

    static constexpr int beatsPerBar = 4;
    static constexpr int barsPerPhrase = 2;

    // Semitones above the root of the major (0 2 4 7 9) or minor (0 3 5 7 10) pentatonic scale.
    static std::array<int, 5> scale(bool minor);
    static double frequency(int midiPitch);
    static Piece compose(quint64 seed);
    static QcSampleBuffer render(const Piece &piece, int sampleRate);
};
