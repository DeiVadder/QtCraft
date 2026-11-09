// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/sound/QcMusicComposer.h"

#include "clientcore/sound/QcMusicReverb.h"
#include "clientcore/sound/QcSoundSynthesizer.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

using Voice = QcMusicComposer::Voice;

constexpr int scaleSize = 7;
constexpr int chordsPerCycle = 4;
// The quietest level of the dynamic arc against its loudest (about -8 dB).
constexpr double quietestLevel = 0.4;
// The melody moves over these degrees of the scale above its octave (a ninth).
constexpr int lowestLeadDegree = 0;
constexpr int highestLeadDegree = 8;

// Which chord may follow which, as degrees of the scale (0 = tonic) with weights: general
// tendencies of tonal harmony, not taken from any piece (decision E-1157).
struct ChordStep
{
    int from = 0;
    std::array<int, 5> next{};
    std::array<int, 5> weight{};
};

constexpr std::array<ChordStep, 6> majorSteps = {{
    {0, {3, 5, 4, 1, 2}, {4, 4, 3, 2, 1}},
    {1, {4, 3, 5, 0, 0}, {5, 3, 1, 1, 0}},
    {2, {5, 3, 1, 0, 0}, {5, 3, 1, 1, 0}},
    {3, {0, 4, 5, 1, 0}, {4, 4, 2, 2, 0}},
    {4, {0, 5, 3, 0, 0}, {6, 3, 2, 0, 0}},
    {5, {3, 1, 4, 2, 0}, {5, 3, 2, 1, 0}},
}};
constexpr std::array<ChordStep, 6> minorSteps = {{
    {0, {5, 3, 6, 2, 4}, {4, 4, 3, 2, 1}},
    {2, {5, 3, 6, 0, 0}, {4, 3, 2, 1, 0}},
    {3, {0, 5, 6, 4, 0}, {4, 3, 3, 1, 0}},
    {4, {0, 5, 0, 0, 0}, {5, 3, 0, 0, 0}},
    {5, {2, 6, 3, 0, 0}, {3, 4, 3, 2, 0}},
    {6, {2, 0, 5, 0, 0}, {4, 3, 3, 0, 0}},
}};

int nextChord(QRandomGenerator &random, int from, bool minor)
{
    const auto &table = minor ? minorSteps : majorSteps;
    const auto step = std::ranges::find(table, from, &ChordStep::from);
    if (step == table.end()) {
        return 0;
    }
    int total = 0;
    for (const int weight : step->weight) {
        total += weight;
    }
    int pick = random.bounded(total);
    for (std::size_t i = 0; i < step->next.size(); ++i) {
        if (pick < step->weight[i]) {
            return step->next[i];
        }
        pick -= step->weight[i];
    }
    return 0;
}

// A cycle of four chords starting on the tonic.
std::array<int, chordsPerCycle> makeCycle(QRandomGenerator &random, bool minor)
{
    std::array<int, chordsPerCycle> cycle{};
    for (std::size_t i = 1; i < cycle.size(); ++i) {
        cycle[i] = nextChord(random, cycle[i - 1], minor);
        if (cycle[i] == cycle[i - 1]) {
            cycle[i] = nextChord(random, cycle[i], minor);
        }
    }
    return cycle;
}

// The chords of the whole piece: two cycles in the form A A B A ..., one chord in a few varied,
// ending on the tonic.
QList<int> makeProgression(QRandomGenerator &random, bool minor, int chords)
{
    const std::array<int, chordsPerCycle> a = makeCycle(random, minor);
    const std::array<int, chordsPerCycle> b = makeCycle(random, minor);
    QList<int> progression;
    for (int chord = 0; chord < chords; ++chord) {
        const int cycle = chord / chordsPerCycle;
        const auto &from = cycle % 4 == 2 ? b : a;
        int degree = from[static_cast<std::size_t>(chord % chordsPerCycle)];
        if (chord % chordsPerCycle != 0 && random.bounded(100) < 15) {
            degree = nextChord(random, progression.constLast(), minor);
        }
        progression.append(degree);
    }
    progression.last() = 0;
    return progression;
}

// A melody step: a degree of the melody's scale (or a rest) and its length in beats.
struct MotifNote
{
    int degree = 0;
    bool rest = false;
    double beats = 1.0;
};

QList<MotifNote> makeMotif(QRandomGenerator &random, double phraseBeats, double notesPerBar, int centre)
{
    static constexpr std::array<double, 5> lengths = {0.5, 1.0, 1.0, 1.5, 2.0};
    static constexpr std::array<int, 7> steps = {-2, -1, -1, 0, 1, 1, 2};
    // The mean of `lengths` is 1.2: this base gives `notesPerBar` notes per bar on average.
    const double base = QcMusicComposer::beatsPerBar / notesPerBar / 1.2;
    QList<MotifNote> motif;
    double t = 0.0;
    int degree = centre;
    while (t < phraseBeats) {
        MotifNote note;
        const double drawn = std::max(0.5, std::round(2.0 * base * lengths[random.bounded(static_cast<int>(lengths.size()))]) / 2.0);
        note.beats = std::min(drawn, phraseBeats - t);
        note.rest = !motif.isEmpty() && random.bounded(100) < 15;
        degree = std::clamp(degree + steps[random.bounded(static_cast<int>(steps.size()))], lowestLeadDegree, highestLeadDegree);
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
            note.degree = std::clamp(note.degree + (random.bounded(2) == 0 ? -1 : 1), lowestLeadDegree, highestLeadDegree);
        }
    }
    return varied;
}

// Writes the voices of one piece from its drawn key, tempo and chords.
class Writer
{
public:
    Writer(QRandomGenerator &random, QcMusicComposer::Piece &piece, const QcMusicArrangement &arrangement, int chords)
        : m_random(random)
        , m_piece(piece)
        , m_arrangement(arrangement)
        , m_steps(QcMusicComposer::scale(piece.minor))
        , m_beat(60.0 / piece.beatsPerMinute)
        , m_chordSeconds(arrangement.barsPerChord * QcMusicComposer::beatsPerBar * m_beat)
        , m_progression(makeProgression(random, piece.minor, chords))
        , m_end(chords * m_chordSeconds)
    {
    }

    [[nodiscard]] double end() const { return m_end; }

    void writeHarmony()
    {
        for (qsizetype chord = 0; chord < progression().size(); ++chord) {
            const double start = static_cast<double>(chord) * chordSeconds();
            const bool last = chord + 1 == progression().size();
            const double length = last ? end() - start : chordSeconds() + 0.3;
            const int degree = progression()[chord];
            for (int tone = 0; tone < 3; ++tone) {
                add(start, length, padPitch(degree + 2 * tone), 0.35f, Voice::Pad);
            }
            // A softer ninth above the triad colours the chord.
            add(start, length, padPitch(degree + 1) + 12, 0.22f, Voice::Pad);
            if (start >= m_arrangement.fadeInSeconds || last) {
                add(start, std::min(length, end() - start), m_piece.root + step(degree) % 12, 0.55f, Voice::Bass);
            }
        }
    }

    void writeMelody()
    {
        const double phraseBeats = 2.0 * m_arrangement.barsPerChord * QcMusicComposer::beatsPerBar;
        const QList<MotifNote> a = makeMotif(m_random, phraseBeats, m_arrangement.leadNotesPerBar, 3);
        const QList<MotifNote> b = makeMotif(m_random, phraseBeats, m_arrangement.leadNotesPerBar, 5);
        const QList<QList<MotifNote>> form = {a, vary(a, m_random), b, a};
        const double phraseSeconds = phraseBeats * beat();
        const auto phrases = static_cast<int>(end() / phraseSeconds);
        int played = 0;
        for (int phrase = 0; phrase < phrases; ++phrase) {
            const double start = phrase * phraseSeconds;
            const double outro = std::max(m_arrangement.fadeOutSeconds, chordSeconds());
            const bool inside = start >= m_arrangement.fadeInSeconds && start + phraseSeconds <= end() - outro;
            if (!inside || m_random.generateDouble() >= m_arrangement.leadShare) {
                continue;
            }
            writePhrase(start, form[played % form.size()]);
            ++played;
        }

        // The melody's last word: the tonic, ringing into the ending.
        const double last = end() - chordSeconds();
        add(last, chordSeconds(), leadPitch(scaleSize), 0.5f, Voice::Lead);
    }

    void writeKeys()
    {
        if (m_arrangement.keysNotesPerBar <= 0.0) {
            return;
        }
        const double spacing = QcMusicComposer::beatsPerBar / m_arrangement.keysNotesPerBar * beat();
        const double first = std::max(m_arrangement.fadeInSeconds, 0.0);
        const auto count = static_cast<int>((end() - chordSeconds() - first) / spacing);
        for (int index = 0; index < count; ++index) {
            const double t = first + index * spacing;
            const int degree = progression()[static_cast<qsizetype>(t / chordSeconds())];
            static constexpr std::array<int, 4> pattern = {0, 2, 4, 2};
            const int pitch = padPitch(degree + pattern[static_cast<std::size_t>(index % 4)]) + 12;
            const auto velocity = static_cast<float>(0.35 + 0.15 * m_random.generateDouble());
            add(t, spacing, pitch, velocity, Voice::Keys);
        }
    }

private:
    [[nodiscard]] double beat() const { return m_beat; }
    [[nodiscard]] double chordSeconds() const { return m_chordSeconds; }
    [[nodiscard]] const QList<int> &progression() const { return m_progression; }

    void writePhrase(double start, const QList<MotifNote> &motif)
    {
        double t = 0.0;
        for (const MotifNote &note : motif) {
            if (!note.rest) {
                const double at = start + t * beat();
                const auto velocity = static_cast<float>(0.5 + 0.25 * m_random.generateDouble());
                add(at, note.beats * beat() * 0.95, leadPitch(onChord(note.degree, at, t)), velocity, Voice::Lead);
            }
            t += note.beats;
        }
    }

    // On the first beat of a bar the melody takes the nearest tone of the chord sounding then.
    [[nodiscard]] int onChord(int degree, double at, double beats) const
    {
        if (std::fmod(beats, QcMusicComposer::beatsPerBar) > 1e-9) {
            return degree;
        }
        const int chord = progression()[std::min(static_cast<qsizetype>(at / chordSeconds()), progression().size() - 1)];
        for (const int offset : {0, -1, 1}) {
            const int candidate = degree + offset;
            const int relative = ((candidate - chord) % scaleSize + scaleSize) % scaleSize;
            if (relative == 0 || relative == 2 || relative == 4) {
                return candidate;
            }
        }
        return degree;
    }

    // Semitones above the root of degree `degree` (any octave).
    [[nodiscard]] int step(int degree) const
    {
        const int octave = degree >= 0 ? degree / scaleSize : (degree - scaleSize + 1) / scaleSize;
        const int inScale = degree - octave * scaleSize;
        return 12 * octave + m_steps[static_cast<std::size_t>(inScale)];
    }

    // A chord tone folded into the octave and a half above the bass.
    [[nodiscard]] int padPitch(int degree) const
    {
        int pitch = m_piece.root + 12 + step(degree) % 12;
        while (pitch > m_piece.root + 22) {
            pitch -= 12;
        }
        return pitch;
    }

    [[nodiscard]] int leadPitch(int degree) const { return m_piece.root + 12 * m_arrangement.leadOctaves + step(degree); }

    // The dynamic arc: rising to the loudest point, falling after it, faded in and out.
    [[nodiscard]] double level(double t) const
    {
        const double share = std::clamp(t / end(), 0.0, 1.0);
        const double peak = std::clamp(m_arrangement.peakAt, 0.01, 0.99);
        const double shape = share <= peak ? share / peak : (1.0 - share) / (1.0 - peak);
        const double fadeIn = m_arrangement.fadeInSeconds > 0.0 ? std::min(1.0, 0.3 + 0.7 * t / m_arrangement.fadeInSeconds) : 1.0;
        const double fadeOut = m_arrangement.fadeOutSeconds > 0.0 ? std::min(1.0, 0.2 + 0.8 * (end() - t) / m_arrangement.fadeOutSeconds) : 1.0;
        return (quietestLevel + (1.0 - quietestLevel) * std::sqrt(shape)) * fadeIn * fadeOut;
    }

    void add(double start, double duration, int pitch, float velocity, Voice voice)
    {
        const auto scaled = static_cast<float>(std::clamp(velocity * level(start), 0.02, 1.0));
        m_piece.notes.append({start, std::min(duration, end() - start), pitch, scaled, voice});
    }

    QRandomGenerator &m_random;
    QcMusicComposer::Piece &m_piece;
    const QcMusicArrangement &m_arrangement;
    const std::array<int, scaleSize> m_steps;
    const double m_beat;
    const double m_chordSeconds;
    const QList<int> m_progression;
    const double m_end;
};

} // namespace

std::array<int, 7> QcMusicComposer::scale(bool minor)
{
    return minor ? std::array<int, 7>{0, 2, 3, 5, 7, 8, 10} : std::array<int, 7>{0, 2, 4, 5, 7, 9, 11};
}

double QcMusicComposer::frequency(int midiPitch)
{
    return 440.0 * std::pow(2.0, (midiPitch - 69) / 12.0);
}

QcMusicComposer::Piece QcMusicComposer::compose(quint64 seed, const QcMusicStyle &style, const QcMusicArrangement &arrangement)
{
    const std::array<quint32, 2> seedWords = {static_cast<quint32>(seed), static_cast<quint32>(seed >> 32)};
    QRandomGenerator random(seedWords.data(), seedWords.data() + seedWords.size());

    Piece piece;
    const bool drawnMinor = random.bounded(2) == 1;
    piece.minor = style.scale == QcMusicStyle::Scale::Any ? drawnMinor : style.scale == QcMusicStyle::Scale::Minor;
    piece.root = style.lowestRoot + random.bounded(style.highestRoot - style.lowestRoot + 1);
    piece.beatsPerMinute = style.slowestTempo + random.bounded(style.fastestTempo - style.slowestTempo + 1);
    piece.lead = style.lead;
    piece.leadLowpass = style.leadLowpass;
    piece.padLowpass = arrangement.padLowpass;
    piece.reverbSeconds = arrangement.reverbSeconds;
    piece.reverbMix = arrangement.reverbMix;

    // Whole cycles of four chords, as many as fit the drawn length (at least two).
    const double longest = std::min(arrangement.longestSeconds, QcMusicArrangement::longestPiece);
    const double shortest = std::min(arrangement.shortestSeconds, longest);
    const double length = shortest + (longest - shortest) * random.generateDouble();
    const double cycleSeconds = chordsPerCycle * arrangement.barsPerChord * beatsPerBar * 60.0 / piece.beatsPerMinute;
    const int cycles = std::max(2, static_cast<int>(length / cycleSeconds));

    Writer writer(random, piece, arrangement, cycles * chordsPerCycle);
    writer.writeHarmony();
    writer.writeMelody();
    writer.writeKeys();
    if (style.drone) {
        piece.notes.append({0.0, writer.end(), piece.root - 12, 0.5f, Voice::Drone});
    }

    piece.seconds = writer.end() + tailSeconds(piece);
    return piece;
}

double QcMusicComposer::tailSeconds(const Piece &piece)
{
    return std::min(piece.reverbSeconds, maxTailSeconds);
}

QcSoundLayer QcMusicComposer::layerOf(const Note &note, const Piece &piece)
{
    QcSoundLayer layer;
    layer.frequency = frequency(note.pitch);
    layer.start = note.start;
    switch (note.voice) {
    case Voice::Lead:
        // A soft, sustained melody that swells in a little.
        layer.wave = piece.lead;
        layer.duration = note.duration + 0.4;
        layer.attack = std::min(0.12, note.duration / 3.0);
        layer.curve = 0.9;
        layer.lowpass = piece.leadLowpass;
        layer.gain = 0.42 * note.velocity;
        break;
    case Voice::Bass:
        layer.wave = QcSoundWave::Sine;
        layer.duration = note.duration + 0.3;
        layer.attack = std::min(0.6, note.duration / 4.0);
        layer.curve = 0.5;
        layer.gain = 0.35 * note.velocity;
        break;
    case Voice::Pad:
        layer.wave = QcSoundWave::Saw;
        layer.duration = note.duration + 0.8;
        layer.attack = std::min(1.5, note.duration / 3.0);
        layer.curve = 0.8;
        layer.lowpass = piece.padLowpass;
        layer.tremoloRate = 0.3;
        layer.tremoloDepth = 0.15;
        layer.gain = 0.26 * note.velocity;
        break;
    case Voice::Keys:
        // A plucked, piano-like tone that dies away on its own.
        layer.wave = QcSoundWave::Triangle;
        layer.duration = note.duration + 1.8;
        layer.attack = 0.006;
        layer.curve = 2.6;
        layer.lowpass = 1800.0;
        layer.gain = 0.4 * note.velocity;
        break;
    case Voice::Drone:
        // A dark, slowly breathing hum that fades in and out with the piece.
        layer.wave = QcSoundWave::Saw;
        layer.duration = note.duration;
        layer.attack = std::min(4.0, note.duration / 4.0);
        layer.curve = 0.5;
        layer.lowpass = 220.0;
        layer.tremoloRate = 0.15;
        layer.tremoloDepth = 0.35;
        layer.gain = 0.22 * note.velocity;
        break;
    }
    return layer;
}

QcSampleBuffer QcMusicComposer::render(const Piece &piece, int sampleRate, const std::atomic<bool> *stop)
{
    QList<QcSoundLayer> layers;
    layers.reserve(piece.notes.size());
    for (const Note &note : piece.notes) {
        layers.append(layerOf(note, piece));
    }

    // Mixed straight into the final length (music and reverb tail), so the buffer is never
    // reallocated; reverb and normalisation work on it in place.
    QcSampleBuffer buffer;
    buffer.sampleRate = sampleRate;
    std::vector<float> mixed = QcSoundSynthesizer::mixLayers(layers, 0x4D555349u, sampleRate, piece.seconds, stop);
    if (mixed.empty()) {
        return buffer;
    }
    mixed = QcMusicReverb(piece.reverbSeconds, piece.reverbMix, sampleRate).processed(std::move(mixed));
    buffer.samples = QcSoundSynthesizer::normalised(std::move(mixed));
    return buffer;
}
