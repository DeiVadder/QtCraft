// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <vector>

/*! \brief The room around a composed piece (decision E-1156): a mono reverb of four damped comb
    filters in parallel and two all-pass filters in series (after Schroeder and Moorer), tuned to
    a reverb time. It works on a rendered buffer without copying it and keeps only its short delay
    lines. */
class QcMusicReverb
{
public:
    /*! A reverb that decays by 60 dB in \a seconds and makes up \a mix (0..1) of the output, at
        \a sampleRate. */
    QcMusicReverb(double seconds, double mix, int sampleRate);

    /*! Returns \a samples with the reverb added; the dry signal keeps its level. */
    [[nodiscard]] std::vector<float> processed(std::vector<float> samples);

private:
    // One comb filter: a delay line fed back through a gentle low-pass (the damping of the air).
    struct Comb
    {
        std::vector<float> line;
        std::size_t at = 0;
        float feedback = 0.0f;
        float damped = 0.0f;
    };
    // One all-pass filter, which smears the echoes of the combs into a dense tail.
    struct AllPass
    {
        std::vector<float> line;
        std::size_t at = 0;
    };

    [[nodiscard]] float wet(float input);

    std::array<Comb, 4> m_combs;
    std::array<AllPass, 2> m_allPasses;
    float m_mix = 0.0f;
};
