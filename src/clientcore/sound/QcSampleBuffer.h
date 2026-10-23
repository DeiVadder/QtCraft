#pragma once

#include <vector>

// Mono PCM audio in [-1, 1]. Immutable once rendered; shared between the mixer's voices.
struct QcSampleBuffer
{
    std::vector<float> samples;
    int sampleRate = 44100;
    // Looping buffers end where they begin, so playing them back to back has no seam.
    bool loop = false;

    double seconds() const { return sampleRate > 0 ? static_cast<double>(samples.size()) / sampleRate : 0.0; }
};
