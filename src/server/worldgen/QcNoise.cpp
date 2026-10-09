#include "QcNoise.h"

#include <cmath>
#include <numbers>

namespace {

quint64 splitMix(quint64 &state)
{
    quint64 z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double fade(double t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double lerp(double t, double a, double b)
{
    return a + t * (b - a);
}

double gradient3(int hash, double x, double y, double z)
{
    const int h = hash & 15;
    const double u = h < 8 ? x : y;
    const double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

double gradient2(int hash, double x, double z)
{
    // Eight directions on the unit square, scaled so the result range matches the 3D variant.
    switch (hash & 7) {
    case 0:
        return x + z;
    case 1:
        return -x + z;
    case 2:
        return x - z;
    case 3:
        return -x - z;
    case 4:
        return std::numbers::sqrt2 * x;
    case 5:
        return -std::numbers::sqrt2 * x;
    case 6:
        return std::numbers::sqrt2 * z;
    default:
        return -std::numbers::sqrt2 * z;
    }
}

} // namespace

QcNoise::QcNoise(quint64 seed)
{
    std::array<quint8, 256> values{};
    for (int i = 0; i < 256; ++i) {
        values[std::size_t(i)] = quint8(i);
    }

    quint64 state = seed;
    // Fisher-Yates shuffle driven by SplitMix64: platform independent, unlike std::shuffle.
    for (int i = 255; i > 0; --i) {
        const auto j = std::size_t(splitMix(state) % quint64(i + 1));
        std::swap(values[std::size_t(i)], values[j]);
    }

    for (std::size_t i = 0; i < 512; ++i) {
        m_permutation[i] = values[i & 255];
    }
}

double QcNoise::noise3(double x, double y, double z) const
{
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const double fz = std::floor(z);
    const int X = int(qint64(fx) & 255);
    const int Y = int(qint64(fy) & 255);
    const int Z = int(qint64(fz) & 255);
    x -= fx;
    y -= fy;
    z -= fz;

    const double u = fade(x);
    const double v = fade(y);
    const double w = fade(z);

    const auto p = [this](int index) { return int(m_permutation[std::size_t(index)]); };
    const int A = p(X) + Y;
    const int AA = p(A) + Z;
    const int AB = p(A + 1) + Z;
    const int B = p(X + 1) + Y;
    const int BA = p(B) + Z;
    const int BB = p(B + 1) + Z;

    return lerp(w,
                lerp(v, lerp(u, gradient3(p(AA), x, y, z), gradient3(p(BA), x - 1, y, z)),
                     lerp(u, gradient3(p(AB), x, y - 1, z), gradient3(p(BB), x - 1, y - 1, z))),
                lerp(v, lerp(u, gradient3(p(AA + 1), x, y, z - 1), gradient3(p(BA + 1), x - 1, y, z - 1)),
                     lerp(u, gradient3(p(AB + 1), x, y - 1, z - 1), gradient3(p(BB + 1), x - 1, y - 1, z - 1))));
}

double QcNoise::noise2(double x, double z) const
{
    const double fx = std::floor(x);
    const double fz = std::floor(z);
    const int X = int(qint64(fx) & 255);
    const int Z = int(qint64(fz) & 255);
    x -= fx;
    z -= fz;

    const double u = fade(x);
    const double w = fade(z);

    const auto p = [this](int index) { return int(m_permutation[std::size_t(index)]); };
    const int A = p(X) + Z;
    const int B = p(X + 1) + Z;

    // Gradients of length up to sqrt(2) keep the output within about [-1, 1], like the 3D variant.
    return lerp(w, lerp(u, gradient2(p(A), x, z), gradient2(p(B), x - 1, z)),
                lerp(u, gradient2(p(A + 1), x, z - 1), gradient2(p(B + 1), x - 1, z - 1)));
}

double QcNoise::fractal2(double x, double z, int octaves, double persistence) const
{
    double sum = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double total = 0.0;

    for (int i = 0; i < octaves; ++i) {
        // Offsetting each octave avoids the shared zero at integer lattice points.
        sum += amplitude * noise2(x * frequency + i * 17.31, z * frequency - i * 9.73);
        total += amplitude;
        amplitude *= persistence;
        frequency *= 2.0;
    }

    return total > 0.0 ? sum / total : 0.0;
}

double QcNoise::fractal3(double x, double y, double z, int octaves, double persistence) const
{
    double sum = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double total = 0.0;

    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * noise3(x * frequency + i * 17.31, y * frequency + i * 5.17, z * frequency - i * 9.73);
        total += amplitude;
        amplitude *= persistence;
        frequency *= 2.0;
    }

    return total > 0.0 ? sum / total : 0.0;
}

quint64 qcMixSeed(quint64 seed, quint64 salt)
{
    quint64 state = seed ^ (salt * 0xD6E8FEB86659FD93ull);
    return splitMix(state);
}

double qcPositionRandom(quint64 seed, int x, int y, int z)
{
    quint64 state = seed ^ (quint64(quint32(x)) * 0x9E3779B97F4A7C15ull) ^ (quint64(quint32(y)) * 0xC2B2AE3D27D4EB4Full)
                    ^ (quint64(quint32(z)) * 0x165667B19E3779F9ull);
    return double(splitMix(state) >> 11) * (1.0 / 9007199254740992.0);
}
