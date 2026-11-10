// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/sound/QcMusicReverb.h"

#include <algorithm>
#include <cmath>

namespace {

// Delays of the combs and all-passes in milliseconds: mutually prime-ish lengths, so their
// echoes do not line up (the usual values of a Schroeder reverb).
constexpr std::array<double, 4> combMilliseconds = {29.7, 37.1, 41.1, 43.7};
constexpr std::array<double, 2> allPassMilliseconds = {5.0, 1.7};
constexpr float allPassGain = 0.7f;
// How much each pass through a comb's loop dulls the highs.
constexpr float damping = 0.25f;

std::size_t delayOf(double milliseconds, int sampleRate)
{
    return std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(milliseconds * 0.001 * sampleRate)));
}

} // namespace

QcMusicReverb::QcMusicReverb(double seconds, double mix, int sampleRate)
    : m_mix(static_cast<float>(std::clamp(mix, 0.0, 1.0)))
{
    const double time = std::max(seconds, 0.05);
    for (std::size_t i = 0; i < m_combs.size(); ++i) {
        // A loop of d seconds loses 60 dB in `time`: gain 10^(-3 d / time) per pass.
        m_combs[i].line.assign(delayOf(combMilliseconds[i], sampleRate), 0.0f);
        m_combs[i].feedback = static_cast<float>(std::pow(10.0, -3.0 * combMilliseconds[i] * 0.001 / time));
    }
    for (std::size_t i = 0; i < m_allPasses.size(); ++i) {
        m_allPasses[i].line.assign(delayOf(allPassMilliseconds[i], sampleRate), 0.0f);
    }
}

float QcMusicReverb::wet(float input)
{
    float sum = 0.0f;
    for (Comb &comb : m_combs) {
        const float delayed = comb.line[comb.at];
        comb.damped = delayed * (1.0f - damping) + comb.damped * damping;
        comb.line[comb.at] = input + comb.damped * comb.feedback;
        comb.at = (comb.at + 1) % comb.line.size();
        sum += delayed;
    }

    float signal = sum * 0.25f;
    for (AllPass &pass : m_allPasses) {
        const float delayed = pass.line[pass.at];
        const float fed = signal + delayed * allPassGain;
        pass.line[pass.at] = fed;
        pass.at = (pass.at + 1) % pass.line.size();
        signal = delayed - fed * allPassGain;
    }
    return signal;
}

std::vector<float> QcMusicReverb::processed(std::vector<float> samples)
{
    for (float &sample : samples) {
        sample += m_mix * wet(sample);
    }
    return samples;
}
