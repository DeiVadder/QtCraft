#include "clientcore/sound/QcSoundSynthesizer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

// xorshift32: small, fast and identical on every platform.
struct Random
{
    quint32 state;

    quint32 next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    // Uniform in [0, 1).
    double unit() { return double(next() >> 8) / double(1u << 24); }
    // Uniform in [-1, 1).
    double signedUnit() { return unit() * 2.0 - 1.0; }
};

quint32 mix(quint32 value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value != 0 ? value : 0x9E3779B9u;
}

double tone(QcSoundWave wave, double phase)
{
    switch (wave) {
    case QcSoundWave::Sine:
        return std::sin(2.0 * std::numbers::pi * phase);
    case QcSoundWave::Square:
        return phase < 0.5 ? 1.0 : -1.0;
    case QcSoundWave::Triangle:
        return 1.0 - 4.0 * std::abs(phase - 0.5);
    case QcSoundWave::Saw:
        return 2.0 * phase - 1.0;
    case QcSoundWave::Noise:
    case QcSoundWave::Crackle:
        break;
    }
    return 0.0;
}

double filterCoefficient(double cutoff, int sampleRate)
{
    return 1.0 - std::exp(-2.0 * std::numbers::pi * cutoff / sampleRate);
}

// One layer while its samples are generated: oscillator or noise source, filters and envelope.
class LayerVoice
{
public:
    LayerVoice(const QcSoundLayer &layer, quint32 seed, bool loop, int sampleRate, std::size_t count)
        : m_layer(layer)
        , m_random{mix(seed)}
        , m_sampleRate(sampleRate)
        , m_count(count)
        , m_sweep(layer.endFrequency > 0.0 && layer.endFrequency != layer.frequency)
        , m_ratio(m_sweep ? layer.endFrequency / layer.frequency : 1.0)
        , m_lowpass(layer.lowpass > 0.0 ? filterCoefficient(layer.lowpass, sampleRate) : 0.0)
        , m_highpass(layer.highpass > 0.0 ? filterCoefficient(layer.highpass, sampleRate) : 0.0)
        , m_crackleChance(layer.density / sampleRate)
        // Each crackle impulse rings for about 1.5 ms.
        , m_crackleDecay(std::exp(-1.0 / (0.0015 * sampleRate)))
        , m_attackSamples(layer.attack * sampleRate)
        , m_fadeSamples(loop ? 0.0 : QcSoundSynthesizer::endFadeSeconds * sampleRate)
    {
    }

    // Filtered source signal of sample `i`.
    double signal(std::size_t i)
    {
        double x = source(i);
        if (m_layer.lowpass > 0.0) {
            // Two one-pole stages: a gentle 12 dB per octave slope.
            m_low1 += m_lowpass * (x - m_low1);
            m_low2 += m_lowpass * (m_low1 - m_low2);
            x = m_low2;
        }
        if (m_layer.highpass > 0.0) {
            m_highState += m_highpass * (x - m_highState);
            x -= m_highState;
        }
        return x;
    }

    // Attack, decay curve, end fade and tremolo at sample `i`.
    double envelope(std::size_t i) const
    {
        double envelope = 1.0;
        if (double(i) < m_attackSamples) {
            envelope = double(i) / m_attackSamples;
        } else if (m_layer.curve > 0.0) {
            const double decay = (double(i) - m_attackSamples) / std::max(1.0, double(m_count) - m_attackSamples);
            envelope = std::pow(std::max(0.0, 1.0 - decay), m_layer.curve);
        }

        const double remaining = double(m_count - i);
        if (remaining < m_fadeSamples) {
            envelope *= remaining / m_fadeSamples;
        }

        if (m_layer.tremoloDepth > 0.0) {
            const double t = double(i) / m_sampleRate;
            envelope *= 1.0 - m_layer.tremoloDepth * (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * m_layer.tremoloRate * t));
        }

        return envelope;
    }

private:
    double source(std::size_t i)
    {
        if (m_layer.wave == QcSoundWave::Noise) {
            return m_random.signedUnit();
        }

        if (m_layer.wave == QcSoundWave::Crackle) {
            if (m_random.unit() < m_crackleChance) {
                m_crackle = (0.5 + 0.5 * m_random.unit()) * (m_random.unit() < 0.5 ? -1.0 : 1.0);
            }
            const double x = m_crackle;
            m_crackle *= m_crackleDecay;
            return x;
        }

        const double progress = double(i) / double(m_count);
        const double x = tone(m_layer.wave, m_phase);
        const double frequency = m_sweep ? m_layer.frequency * std::pow(m_ratio, progress) : m_layer.frequency;
        m_phase += frequency / m_sampleRate;
        m_phase -= std::floor(m_phase);
        return x;
    }

    const QcSoundLayer &m_layer;
    Random m_random;
    int m_sampleRate = 0;
    std::size_t m_count = 0;
    bool m_sweep = false;
    double m_ratio = 1.0;
    double m_lowpass = 0.0;
    double m_highpass = 0.0;
    double m_crackleChance = 0.0;
    double m_crackleDecay = 0.0;
    double m_attackSamples = 0.0;
    double m_fadeSamples = 0.0;
    double m_phase = 0.0;
    double m_low1 = 0.0;
    double m_low2 = 0.0;
    double m_highState = 0.0;
    double m_crackle = 0.0;
};

void renderLayer(const QcSoundLayer &layer, quint32 seed, bool loop, int sampleRate, std::vector<float> &out)
{
    const auto first = std::size_t(std::max(0LL, std::llround(layer.start * sampleRate)));
    const auto count = std::size_t(std::max(0LL, std::llround(layer.duration * sampleRate)));
    if (count == 0 || first >= out.size()) {
        return;
    }

    LayerVoice voice(layer, seed, loop, sampleRate, count);
    const std::size_t end = std::min(count, out.size() - first);
    for (std::size_t i = 0; i < end; ++i) {
        const double x = voice.signal(i);
        out[first + i] += float(layer.gain * voice.envelope(i) * x);
    }
}

} // namespace

quint32 QcSoundSynthesizer::seedOf(const QString &name, int variant)
{
    // FNV-1a over the UTF-16 code units: stable across runs, unlike qHash.
    quint32 hash = 2166136261u;
    for (const QChar c : name) {
        hash ^= c.unicode();
        hash *= 16777619u;
    }
    return mix(hash ^ (quint32(variant) * 0x9E3779B9u));
}

QcSampleBuffer QcSoundSynthesizer::render(const QcSoundDefinition &sound, int variant, int sampleRate)
{
    return renderLayers(sound.layers, seedOf(sound.name, variant), sound.loop, sampleRate);
}

QcSampleBuffer QcSoundSynthesizer::renderLayers(const QList<QcSoundLayer> &layers, quint32 seed, bool loop, int sampleRate)
{
    QcSampleBuffer buffer;
    buffer.sampleRate = sampleRate;
    buffer.loop = loop;

    double seconds = 0.0;
    for (const QcSoundLayer &layer : layers) {
        if (std::isfinite(layer.start) && std::isfinite(layer.duration)) {
            seconds = std::max(seconds, layer.start + layer.duration);
        }
    }

    if (sampleRate <= 0 || seconds <= 0.0) {
        return buffer;
    }

    std::vector<float> &out = buffer.samples;
    out.assign(std::size_t(std::llround(seconds * sampleRate)), 0.0f);
    for (qsizetype index = 0; index < layers.size(); ++index) {
        renderLayer(layers[index], seed ^ (quint32(index + 1) * 0x85EBCA6Bu), loop, sampleRate, out);
    }

    if (loop) {
        // Blend the tail into the head and cut the tail off: the last sample then leads straight
        // into the first. Equal-power weights keep the level of uncorrelated noise steady.
        const auto blend = std::size_t(std::min(double(out.size()) * loopBlendShare, maxLoopBlendSeconds * sampleRate));
        if (blend >= 1 && out.size() > 2 * blend) {
            const std::size_t tail = out.size() - blend;
            for (std::size_t i = 0; i < blend; ++i) {
                const double w = double(i) / double(blend);
                out[i] = float(out[i] * std::sqrt(w) + out[tail + i] * std::sqrt(1.0 - w));
            }
            out.resize(tail);
        }
    }

    float loudest = 0.0f;
    for (float &sample : out) {
        if (!std::isfinite(sample)) {
            sample = 0.0f;
        }
        loudest = std::max(loudest, std::abs(sample));
    }

    if (loudest > 0.0f) {
        const float scale = peak / loudest;
        for (float &sample : out) {
            sample *= scale;
        }
    }

    return buffer;
}
