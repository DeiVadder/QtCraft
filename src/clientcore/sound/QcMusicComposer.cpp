// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/sound/QcMusicComposer.h"

#include "clientcore/sound/QcSoundSynthesizer.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

namespace {

// A melody step: a degree of the two-octave melody scale (or a rest) and its length in beats.
struct MotifNote
{
    int degree = 0;
    bool rest = false;
    double beats = 1.0;
};

constexpr int melodyDegrees = 10;
constexpr int phraseBeats = QcMusicComposer::beatsPerBar * QcMusicComposer::barsPerPhrase;

QList<MotifNote> makeMotif(QRandomGenerator &random, int centre)
{
    static constexpr std::array<double, 6> lengths = {0.5, 1.0, 1.0, 1.0, 1.5, 2.0};
    static constexpr std::array<int, 7> steps = {-2, -1, -1, 0, 1, 1, 2};
    QList<MotifNote> motif;
    double t = 0.0;
    int degree = std::clamp(centre + random.bounded(3) - 1, 0, melodyDegrees - 1);

    while (t < phraseBeats) {
        MotifNote note;
        note.beats = std::min(lengths[random.bounded(static_cast<int>(lengths.size()))], phraseBeats - t);
        note.rest = !motif.isEmpty() && random.bounded(100) < 12;
        degree = std::clamp(degree + steps[random.bounded(static_cast<int>(steps.size()))], 0, melodyDegrees - 1);
        note.degree = degree;
        motif.append(note);
        t += note.beats;
    }
    return motif;
}

QList<MotifNote> vary(const QList<MotifNote> &motif, QRandomGenerator &random)
{
    QList<MotifNote> varied = motif;
    for (MotifNote &note : varied) {
        if (random.bounded(100) < 30) {
            note.degree = std::clamp(note.degree + (random.bounded(2) == 0 ? -1 : 1), 0, melodyDegrees - 1);
        }
    }
    return varied;
}

} // namespace

std::array<int, 5> QcMusicComposer::scale(bool minor)
{
    return minor ? std::array<int, 5>{0, 3, 5, 7, 10} : std::array<int, 5>{0, 2, 4, 7, 9};
}

double QcMusicComposer::frequency(int midiPitch)
{
    return 440.0 * std::pow(2.0, (midiPitch - 69) / 12.0);
}

QcMusicComposer::Piece QcMusicComposer::compose(quint64 seed)
{
    const std::array<quint32, 2> seedWords = {static_cast<quint32>(seed), static_cast<quint32>(seed >> 32)};
    QRandomGenerator random(seedWords.data(), seedWords.data() + seedWords.size());

    Piece piece;
    piece.minor = random.bounded(2) == 1;
    piece.root = 43 + random.bounded(7);
    piece.beatsPerMinute = 66.0 + random.bounded(25);

    const double beat = 60.0 / piece.beatsPerMinute;
    const std::array<int, 5> steps = scale(piece.minor);
    const auto melodyPitch = [&](int degree) { return piece.root + 24 + 12 * (degree / 5) + steps[static_cast<std::size_t>(degree % 5)]; };

    const QList<MotifNote> a = makeMotif(random, 3);
    const QList<MotifNote> b = makeMotif(random, 5);
    const QList<QList<MotifNote>> phrases = {a, vary(a, random), b, a};
    for (qsizetype phrase = 0; phrase < phrases.size(); ++phrase) {
        double t = static_cast<double>(phrase * phraseBeats);
        for (const MotifNote &step : phrases[phrase]) {
            if (!step.rest) {
                const auto velocity = static_cast<float>(0.55 + 0.25 * random.generateDouble());
                piece.notes.append({t * beat, step.beats * beat * 0.95, melodyPitch(step.degree), velocity, Voice::Lead});
            }
            t += step.beats;
        }
    }

    const int bars = static_cast<int>(phrases.size()) * barsPerPhrase;
    static constexpr std::array<int, 4> bassDegrees = {0, 3, 4, 0};
    for (int bar = 0; bar < bars; ++bar) {
        const int pitch = piece.root + steps[static_cast<std::size_t>(bassDegrees[static_cast<std::size_t>(bar % 4)])];
        piece.notes.append({bar * beatsPerBar * beat, beatsPerBar * beat * 0.98, pitch, 0.6f, Voice::Bass});
    }

    // Held chord under the B phrase.
    const double bStart = 2.0 * phraseBeats * beat;
    for (const int degree : {0, 2, 3}) {
        piece.notes.append({bStart, phraseBeats * beat, piece.root + 12 + steps[static_cast<std::size_t>(degree)], 0.35f, Voice::Pad});
    }

    // Ending: root in all voices for one bar.
    const double end = static_cast<double>(bars * beatsPerBar) * beat;
    const double last = beatsPerBar * beat;
    piece.notes.append({end, last, melodyPitch(5), 0.6f, Voice::Lead});
    piece.notes.append({end, last, piece.root, 0.6f, Voice::Bass});
    for (const int degree : {0, 2, 3}) {
        piece.notes.append({end, last, piece.root + 12 + steps[static_cast<std::size_t>(degree)], 0.3f, Voice::Pad});
    }

    piece.seconds = end + last;
    return piece;
}

QcSampleBuffer QcMusicComposer::render(const Piece &piece, int sampleRate)
{
    QList<QcSoundLayer> layers;
    layers.reserve(piece.notes.size());
    for (const Note &note : piece.notes) {
        QcSoundLayer layer;
        layer.frequency = frequency(note.pitch);
        layer.start = note.start;

        switch (note.voice) {
        case Voice::Lead:
            layer.wave = QcSoundWave::Triangle;
            layer.duration = note.duration + 0.3;
            layer.attack = 0.02;
            layer.curve = 1.2;
            layer.lowpass = 3000.0;
            layer.gain = 0.5 * note.velocity;
            break;
        case Voice::Bass:
            layer.wave = QcSoundWave::Sine;
            layer.duration = note.duration + 0.2;
            layer.attack = 0.04;
            layer.curve = 0.8;
            layer.gain = 0.6 * note.velocity;
            break;
        case Voice::Pad:
            layer.wave = QcSoundWave::Saw;
            layer.duration = note.duration + 0.5;
            layer.attack = std::min(0.8, note.duration / 3.0);
            layer.curve = 0.6;
            layer.lowpass = 800.0;
            layer.tremoloRate = 0.3;
            layer.tremoloDepth = 0.2;
            layer.gain = 0.25 * note.velocity;
            break;
        }

        layers.append(layer);
    }

    return QcSoundSynthesizer::renderLayers(layers, 0x4D555349u, false, sampleRate);
}
