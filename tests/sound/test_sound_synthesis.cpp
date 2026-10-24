#include "clientcore/sound/QcMusicComposer.h"
#include "clientcore/sound/QcSoundBank.h"
#include "clientcore/sound/QcSoundSynthesizer.h"

#include <QSet>
#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int rate = QcSoundSynthesizer::defaultSampleRate;

QcSoundLayer layer(QcSoundWave wave, double duration, double frequency = 440.0)
{
    QcSoundLayer result;
    result.wave = wave;
    result.duration = duration;
    result.frequency = frequency;
    result.attack = 0.0;
    result.curve = 0.0;
    return result;
}

QcSoundDefinition sound(const QString &name, QList<QcSoundLayer> layers)
{
    QcSoundDefinition definition;
    definition.name = name;
    definition.layers = std::move(layers);
    return definition;
}

float peakOf(const std::vector<float> &samples, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1))
{
    float peak = 0.0f;
    for (std::size_t i = from; i < std::min(to, samples.size()); ++i) {
        peak = std::max(peak, std::abs(samples[i]));
    }
    return peak;
}

double rms(const std::vector<float> &samples, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        sum += static_cast<double>(samples[i]) * samples[i];
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

// Upward zero crossings between two sample indices: about one per period of a tone.
int upwardCrossings(const std::vector<float> &samples, std::size_t from, std::size_t to)
{
    int crossings = 0;
    for (std::size_t i = from + 1; i < to; ++i) {
        crossings += samples[i - 1] < 0.0f && samples[i] >= 0.0f ? 1 : 0;
    }
    return crossings;
}

// Mean size of the steps between neighbouring samples relative to the signal level: large for
// bright (high-frequency) signals, small for dull ones.
double roughness(const std::vector<float> &samples)
{
    double steps = 0.0;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        steps += std::abs(samples[i] - samples[i - 1]);
    }
    return steps / static_cast<double>(samples.size() - 1) / rms(samples, 0, samples.size());
}

} // namespace

// Procedural sound synthesis, the sound bank and the music composer.
class TestSoundSynthesis : public QObject
{
    Q_OBJECT

private slots:
    void renderingIsDeterministicPerNameAndVariant()
    {
        QcSoundLayer tone = layer(QcSoundWave::Sine, 0.2, 660.0);
        tone.curve = 2.0;
        const QcSoundDefinition definition = sound(u"t:knock"_s, {layer(QcSoundWave::Noise, 0.2), tone});
        const QcSampleBuffer first = QcSoundSynthesizer::render(definition, 0);
        const QcSampleBuffer again = QcSoundSynthesizer::render(definition, 0);
        QVERIFY(first.samples == again.samples);
        QVERIFY(QcSoundSynthesizer::render(definition, 1).samples != first.samples);
        QVERIFY(QcSoundSynthesizer::render(sound(u"t:other"_s, definition.layers), 0).samples != first.samples);
        QCOMPARE(QcSoundSynthesizer::seedOf(u"t:knock"_s, 2), QcSoundSynthesizer::seedOf(u"t:knock"_s, 2));
        QVERIFY(QcSoundSynthesizer::seedOf(u"t:knock"_s, 2) != QcSoundSynthesizer::seedOf(u"t:knock"_s, 3));
    }

    void lengthAndLevelAreBounded()
    {
        QcSoundLayer late = layer(QcSoundWave::Square, 0.1, 300.0);
        late.start = 0.2;
        late.gain = 1.0;
        QcSoundLayer noise = layer(QcSoundWave::Noise, 0.25);
        noise.gain = 1.0;
        const QcSampleBuffer buffer = QcSoundSynthesizer::render(sound(u"t:a"_s, {noise, late, layer(QcSoundWave::Saw, 0.3, 100.0)}), 0);

        // The latest layer ends at 0.3 s.
        QCOMPARE(buffer.samples.size(), std::size_t(std::lround(0.3 * rate)));
        QCOMPARE(buffer.sampleRate, rate);
        QVERIFY(!buffer.loop);

        // Three full-gain layers would exceed 1; the rendering is normalised to the peak level.
        QVERIFY(std::abs(peakOf(buffer.samples) - QcSoundSynthesizer::peak) < 1e-6f);
        QVERIFY(std::ranges::all_of(buffer.samples, [](float sample) { return std::isfinite(sample); }));
    }

    void silentLayersGiveSilence()
    {
        QcSoundLayer quiet = layer(QcSoundWave::Noise, 0.1);
        quiet.gain = 0.0;
        const QcSampleBuffer buffer = QcSoundSynthesizer::render(sound(u"t:quiet"_s, {quiet}), 0);
        QCOMPARE(buffer.samples.size(), std::size_t(std::lround(0.1 * rate)));
        QCOMPARE(peakOf(buffer.samples), 0.0f);
        QVERIFY(QcSoundSynthesizer::renderLayers({}, 1, false).samples.empty());
    }

    void layersStartLateAndFadeOut()
    {
        QcSoundLayer delayed = layer(QcSoundWave::Sine, 0.3, 500.0);
        delayed.start = 0.2;
        const QcSampleBuffer buffer = QcSoundSynthesizer::render(sound(u"t:late"_s, {delayed}), 0);
        const auto start = static_cast<std::size_t>(0.2 * rate);
        QCOMPARE(peakOf(buffer.samples, 0, start), 0.0f);
        QVERIFY(peakOf(buffer.samples, start, start + 200) > 0.1f);
        // A held tone still ends at zero rather than with a click.
        QVERIFY(std::abs(buffer.samples.back()) < 0.01f);
        QVERIFY(peakOf(buffer.samples, buffer.samples.size() - 50, buffer.samples.size()) < 0.2f);
    }

    void tonesHaveTheirFrequency()
    {
        const QcSampleBuffer tone = QcSoundSynthesizer::render(sound(u"t:a"_s, {layer(QcSoundWave::Sine, 0.5, 1000.0)}), 0);
        // 1000 Hz for 0.5 s: 500 periods.
        const int crossings = upwardCrossings(tone.samples, 0, tone.samples.size());
        QVERIFY2(std::abs(crossings - 500) <= 2, qPrintable(QString::number(crossings)));
        const QcSampleBuffer triangle = QcSoundSynthesizer::render(sound(u"t:a"_s, {layer(QcSoundWave::Triangle, 0.5, 220.0)}), 0);
        QVERIFY(std::abs(upwardCrossings(triangle.samples, 0, triangle.samples.size()) - 110) <= 2);

        // A sweep from 200 to 800 Hz over one second is low at the start and high at the end.
        QcSoundLayer sweep = layer(QcSoundWave::Sine, 1.0, 200.0);
        sweep.endFrequency = 800.0;
        const QcSampleBuffer swept = QcSoundSynthesizer::render(sound(u"t:a"_s, {sweep}), 0);
        const auto tenth = static_cast<std::size_t>(rate / 10);
        const int early = upwardCrossings(swept.samples, 0, tenth);
        const int late = upwardCrossings(swept.samples, swept.samples.size() - tenth, swept.samples.size());

        // About 21 and 75 periods (200 * 4^0.05 Hz and 800 * 4^-0.05 Hz for a tenth of a second).
        QVERIFY2(early >= 19 && early <= 23, qPrintable(QString::number(early)));
        QVERIFY2(late >= 72 && late <= 78, qPrintable(QString::number(late)));
    }

    void envelopeDecays()
    {
        QcSoundLayer hit = layer(QcSoundWave::Noise, 0.5);
        hit.attack = 0.01;
        hit.curve = 2.0;
        const QcSampleBuffer buffer = QcSoundSynthesizer::render(sound(u"t:hit"_s, {hit}), 0);
        const std::size_t n = buffer.samples.size();
        // (1 - t)^2: the level after 10 % of the decay is far above the level after 90 %.
        QVERIFY(rms(buffer.samples, n / 10, n / 5) > 10.0 * rms(buffer.samples, n * 8 / 10, n * 9 / 10));
        // The attack ramps up from silence.
        QVERIFY(std::abs(buffer.samples[0]) < 1e-6f);
    }

    void filtersShapeTheSpectrum()
    {
        const QcSampleBuffer white = QcSoundSynthesizer::render(sound(u"t:n"_s, {layer(QcSoundWave::Noise, 0.5)}), 0);
        QcSoundLayer dull = layer(QcSoundWave::Noise, 0.5);
        dull.lowpass = 300.0;
        const QcSampleBuffer low = QcSoundSynthesizer::render(sound(u"t:n"_s, {dull}), 0);
        QcSoundLayer thin = layer(QcSoundWave::Sine, 0.5, 100.0);
        thin.highpass = 2000.0;
        QVERIFY(roughness(low.samples) < 0.25 * roughness(white.samples));

        // A high-pass far above a tone's frequency leaves about 100/2000 of it. Normalisation hides
        // the level of a lone tone, so a faint noise layer serves as the reference: next to the
        // weakened tone it dominates the steps, next to the full tone it does not.
        QList<QcSoundLayer> filteredMix = {layer(QcSoundWave::Noise, 0.5), thin};
        filteredMix[0].gain = 0.01;
        QList<QcSoundLayer> plainMix = filteredMix;
        plainMix[1].highpass = 0.0;
        const QcSampleBuffer filtered = QcSoundSynthesizer::renderLayers(filteredMix, 5, false);
        const QcSampleBuffer plain = QcSoundSynthesizer::renderLayers(plainMix, 5, false);
        QVERIFY(roughness(filtered.samples) > 4.0 * roughness(plain.samples));
    }

    // The resonant band-pass (decision E-879): noise through it rings at the centre, about one
    // period per 1/centre seconds, narrower with more resonance; it is stable up to the top of the range.
    void bandPassRingsAtItsCentre()
    {
        QcSoundLayer ring = layer(QcSoundWave::Noise, 0.5);
        ring.bandpass = 1000.0;
        ring.resonance = 12.0;
        // The band's slow flanks let some high noise through, which adds crossings; a low-pass far
        // above the centre takes it away, so the count shows the centre.
        ring.lowpass = 1500.0;
        const QcSampleBuffer narrow = QcSoundSynthesizer::render(sound(u"t:ring"_s, {ring}), 0);
        const int crossings = upwardCrossings(narrow.samples, 0, narrow.samples.size());
        QVERIFY2(std::abs(crossings - 500) < 60, qPrintable(QString::number(crossings)));
        ring.lowpass = 0.0;

        // The same band without the low-pass, once resonant and once wide open: only the resonance
        // differs, and the wide band lets the noise around the centre through, so it is rougher.
        const QcSampleBuffer resonant = QcSoundSynthesizer::render(sound(u"t:ring"_s, {ring}), 0);
        QcSoundLayer wide = ring;
        wide.resonance = QcSoundDefinition::minResonance;
        const QcSampleBuffer open = QcSoundSynthesizer::render(sound(u"t:ring"_s, {wide}), 0);
        QVERIFY2(roughness(open.samples) > 1.5 * roughness(resonant.samples),
                 qPrintable(u"%1 vs %2"_s.arg(roughness(open.samples)).arg(roughness(resonant.samples))));

        QcSoundLayer top = ring;
        top.bandpass = QcSoundDefinition::maxFrequency;
        top.resonance = QcSoundDefinition::maxResonance;
        const QcSampleBuffer high = QcSoundSynthesizer::render(sound(u"t:top"_s, {top}), 0);
        QVERIFY(std::ranges::all_of(high.samples, [](float sample) { return std::isfinite(sample) && std::abs(sample) <= QcSoundSynthesizer::peak + 1e-6f; }));
        QVERIFY(peakOf(high.samples) > 0.5f);
    }

    // Drive (decision E-879) lifts the quieter parts towards the peak without raising the peak.
    void driveRaisesTheLevelNotThePeak()
    {
        QcSoundLayer decaying = layer(QcSoundWave::Noise, 0.5);
        decaying.curve = 3.0;
        QcSoundDefinition plain = sound(u"t:blast"_s, {decaying});
        QcSoundDefinition driven = plain;
        driven.drive = 3.0;
        const QcSampleBuffer quiet = QcSoundSynthesizer::render(plain, 0);
        const QcSampleBuffer loud = QcSoundSynthesizer::render(driven, 0);
        QCOMPARE(loud.samples.size(), quiet.samples.size());
        QVERIFY(std::abs(peakOf(loud.samples) - QcSoundSynthesizer::peak) < 1e-3f);
        QVERIFY(rms(loud.samples, 0, loud.samples.size()) > 1.5 * rms(quiet.samples, 0, quiet.samples.size()));
    }

    void loopsAreSeamless()
    {
        QcSoundLayer wind = layer(QcSoundWave::Noise, 1.0);
        wind.lowpass = 800.0;
        QcSoundDefinition loop = sound(u"t:wind"_s, {wind});
        loop.loop = true;
        const QcSampleBuffer buffer = QcSoundSynthesizer::render(loop, 0);
        QVERIFY(buffer.loop);

        // One second, minus the blended quarter.
        QCOMPARE(buffer.samples.size(), std::size_t(rate - rate / 4));
        float largestStep = 0.0f;
        for (std::size_t i = 1; i < buffer.samples.size(); ++i) {
            largestStep = std::max(largestStep, std::abs(buffer.samples[i] - buffer.samples[i - 1]));
        }
        const float seam = std::abs(buffer.samples.front() - buffer.samples.back());
        QVERIFY2(seam <= largestStep, qPrintable(u"%1 > %2"_s.arg(seam).arg(largestStep)));

        // Long loops blend at most 0.3 s.
        QcSoundDefinition longLoop = loop;
        longLoop.layers[0].duration = 4.0;
        QCOMPARE(QcSoundSynthesizer::render(longLoop, 0).samples.size(), std::size_t(4L * rate - std::lround(0.3 * rate)));
    }

    void crackleIsSparse()
    {
        QcSoundLayer crackle = layer(QcSoundWave::Crackle, 1.0);
        crackle.density = 50.0;
        const QcSampleBuffer buffer = QcSoundSynthesizer::render(sound(u"t:fire"_s, {crackle}), 0);
        // About 50 impulses of a few milliseconds each: most of the second is (nearly) silent.
        const auto loud = std::ranges::count_if(buffer.samples, [](float sample) { return std::abs(sample) > 0.05f; });
        QVERIFY2(loud > 100 && loud < rate / 3, qPrintable(QString::number(loud)));
    }

    void bankRendersEveryVariantWithinItsBudget()
    {
        QcContentRegistry content;
        QcSoundDefinition small = sound(u"t:small"_s, {layer(QcSoundWave::Noise, 0.1)});
        small.variants = 3;
        content.assets().registerSound(small);

        // Each long sound needs 10 s x 8 variants = 3,528,000 samples: four fit into the budget.
        for (int i = 0; i < 6; ++i) {
            QcSoundDefinition big = sound(u"t:big%1"_s.arg(i), {layer(QcSoundWave::Noise, QcSoundDefinition::maxLayerSeconds)});
            big.variants = QcSoundDefinition::maxVariants;
            content.assets().registerSound(big);
        }

        const QcSoundBank bank = QcSoundBank::build(content, rate);
        const QcSoundBank::Entry *entry = bank.find(u"t:small"_s);
        QVERIFY(entry);
        QCOMPARE(entry->variants.size(), 3);
        QVERIFY(entry->variants[0]->samples != entry->variants[1]->samples);
        QVERIFY(entry->definition == small);
        QVERIFY(!bank.find(u"t:missing"_s));
        QCOMPARE(bank.skipped(), (QStringList{u"t:big4"_s, u"t:big5"_s}));
        QCOMPARE(bank.size(), 5);
        QCOMPARE(bank.totalSamples(), qsizetype(3 * 4410 + 4 * 8 * 441000));
        QVERIFY(bank.totalSamples() <= QcSoundBank::maxSamples);
    }

    void musicIsComposedDeterministicallyOnAPentatonicScale()
    {
        const QcMusicComposer::Piece piece = QcMusicComposer::compose(42);
        const QcMusicComposer::Piece again = QcMusicComposer::compose(42);
        QVERIFY(piece.notes == again.notes);
        QCOMPARE(piece.root, again.root);
        QVERIFY(QcMusicComposer::compose(43).notes != piece.notes);

        QVERIFY(piece.beatsPerMinute >= 66.0 && piece.beatsPerMinute <= 90.0);
        // Four phrases of two bars plus one closing bar: 36 beats.
        QCOMPARE(piece.seconds, 36.0 * 60.0 / piece.beatsPerMinute);

        const QList<int> scale = piece.minor ? QList<int>{0, 3, 5, 7, 10} : QList<int>{0, 2, 4, 7, 9};
        QList<QcMusicComposer::Note> lead;
        int bass = 0;
        for (const QcMusicComposer::Note &note : piece.notes) {
            QVERIFY2(scale.contains(((note.pitch - piece.root) % 12 + 12) % 12), qPrintable(QString::number(note.pitch)));
            QVERIFY(note.start >= 0.0 && note.duration > 0.0 && note.start + note.duration <= piece.seconds + 1e-9);
            QVERIFY(note.velocity > 0.0f && note.velocity <= 1.0f);
            if (note.voice == QcMusicComposer::Voice::Lead) {
                lead.append(note);
            }
            bass += note.voice == QcMusicComposer::Voice::Bass ? 1 : 0;
        }
        QCOMPARE(bass, 9);

        std::ranges::sort(lead, {}, &QcMusicComposer::Note::start);
        for (qsizetype i = 1; i < lead.size(); ++i) {
            QVERIFY(lead[i].start >= lead[i - 1].start + lead[i - 1].duration - 1e-9);
        }

        // The melody ends on the root, ringing until the end of the piece.
        QCOMPARE((lead.constLast().pitch - piece.root) % 12, 0);
        QCOMPARE(lead.constLast().start + lead.constLast().duration, piece.seconds);

        // Seeds choose the key, so both scales appear.
        QSet<bool> modes;
        for (quint64 seed = 1; seed <= 20; ++seed) {
            modes.insert(QcMusicComposer::compose(seed).minor);
        }
        QCOMPARE(modes.size(), 2);
    }

    void musicRendersWithoutClipping()
    {
        const QcMusicComposer::Piece piece = QcMusicComposer::compose(7);
        const int musicRate = 22050;
        const QcSampleBuffer buffer = QcMusicComposer::render(piece, musicRate);
        QCOMPARE(buffer.sampleRate, musicRate);

        // The last notes ring on for at most half a second.
        QVERIFY(double(buffer.samples.size()) >= piece.seconds * musicRate);
        QVERIFY(double(buffer.samples.size()) <= (piece.seconds + 0.5) * musicRate + 1.0);
        QVERIFY(std::abs(peakOf(buffer.samples) - QcSoundSynthesizer::peak) < 1e-6f);
        QVERIFY(std::ranges::all_of(buffer.samples, [](float sample) { return std::isfinite(sample); }));

        // Music, not silence: every second has sound.
        for (int second = 0; second + 1 < static_cast<int>(piece.seconds); ++second) {
            QVERIFY2(rms(buffer.samples, std::size_t(second * musicRate), std::size_t((second + 1) * musicRate)) > 0.01,
                     qPrintable(QString::number(second)));
        }
    }
};

QTEST_GUILESS_MAIN(TestSoundSynthesis)
#include "test_sound_synthesis.moc"
