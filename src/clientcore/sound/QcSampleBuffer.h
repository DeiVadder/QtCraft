// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

/*! \brief Mono PCM audio in [-1, 1].

    Immutable once rendered; shared between the mixer's voices. */
struct QcSampleBuffer
{
    /*! The samples, in [-1, 1]. */
    std::vector<float> samples;
    /*! Samples per second. */
    int sampleRate = 44100;
    /*! Looping buffers end where they begin, so playing them back to back has no seam. */
    bool loop = false;

    /*! Returns the length in seconds; 0 without a sample rate. */
    double seconds() const { return sampleRate > 0 ? static_cast<double>(samples.size()) / sampleRate : 0.0; }
};
