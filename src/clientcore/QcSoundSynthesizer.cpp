#include "QcSoundSynthesizer.h"

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

void renderLayer(const QcSoundLayer &layer, quint32 seed, bool loop, int sampleRate, std::vector<float> &out)
{
    const auto first = std::size_t(std::max(0LL, std::llround(layer.start * sampleRate)));
    const auto count = std::size_t(std::max(0LL, std::llround(layer.duration * sampleRate)));
    if (count == 0 || first >= out.size()) {
        return;
    }
    Random random{mix(seed)};
    const bool sweep = layer.endFrequency > 0.0 && layer.endFrequency != layer.frequency;
    const double ratio = sweep ? layer.endFrequency / layer.frequency : 1.0;
    const double lowpass = layer.lowpass > 0.0 ? filterCoefficient(layer.lowpass, sampleRate) : 0.0;
    const double highpass = layer.highpass > 0.0 ? filterCoefficient(layer.highpass, sampleRate) : 0.0;
    const double crackleChance = layer.density / sampleRate;
    // Each crackle impulse rings for about 1.5 ms.
    const double crackleDecay = std::exp(-1.0 / (0.0015 * sampleRate));
    const double attackSamples = layer.attack * sampleRate;
    const double fadeSamples = loop ? 0.0 : QcSoundSynthesizer::endFadeSeconds * sampleRate;
    double phase = 0.0;
    double low1 = 0.0;
    double low2 = 0.0;
    double highState = 0.0;
    double crackle = 0.0;
    const std::size_t end = std::min(count, out.size() - first);
    for (std::size_t i = 0; i < end; ++i) {
        const double progress = double(i) / double(count);
        double x = 0.0;
        if (layer.wave == QcSoundWave::Noise) {
            x = random.signedUnit();
        } else if (layer.wave == QcSoundWave::Crackle) {
            if (random.unit() < crackleChance) {
                crackle = (0.5 + 0.5 * random.unit()) * (random.unit() < 0.5 ? -1.0 : 1.0);
            }
            x = crackle;
            crackle *= crackleDecay;
        } else {
            x = tone(layer.wave, phase);
            const double frequency = sweep ? layer.frequency * std::pow(ratio, progress) : layer.frequency;
            phase += frequency / sampleRate;
            phase -= std::floor(phase);
        }
        if (layer.lowpass > 0.0) {
            // Two one-pole stages: a gentle 12 dB per octave slope.
            low1 += lowpass * (x - low1);
            low2 += lowpass * (low1 - low2);
            x = low2;
        }
        if (layer.highpass > 0.0) {
            highState += highpass * (x - highState);
            x -= highState;
        }
        double envelope = 1.0;
        if (double(i) < attackSamples) {
            envelope = double(i) / attackSamples;
        } else if (layer.curve > 0.0) {
            const double decay = (double(i) - attackSamples) / std::max(1.0, double(count) - attackSamples);
            envelope = std::pow(std::max(0.0, 1.0 - decay), layer.curve);
        }
        const double remaining = double(count - i);
        if (remaining < fadeSamples) {
            envelope *= remaining / fadeSamples;
        }
        if (layer.tremoloDepth > 0.0) {
            const double t = double(i) / sampleRate;
            envelope *= 1.0 - layer.tremoloDepth * (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * layer.tremoloRate * t));
        }
        out[first + i] += float(layer.gain * envelope * x);
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
