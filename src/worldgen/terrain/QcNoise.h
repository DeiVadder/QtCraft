#pragma once

#include <QString>

#include <QtGlobal>

#include <array>

// Seeded gradient noise (improved Perlin) in 2D and 3D with fractal sums. Output of noise2/noise3
// lies roughly in [-1, 1]; identical seeds give identical values on every platform.
class QcNoise
{
public:
    explicit QcNoise(quint64 seed);

    double noise2(double x, double z) const;
    double noise3(double x, double y, double z) const;

    // Fractal Brownian motion: `octaves` layers, each with doubled frequency and `persistence`
    // times the amplitude, normalised back to about [-1, 1].
    double fractal2(double x, double z, int octaves, double persistence = 0.5) const;
    double fractal3(double x, double y, double z, int octaves, double persistence = 0.5) const;

private:
    std::array<quint8, 512> m_permutation{};
};

// Stateless 64-bit mixing used to derive sub-seeds and per-position random values.
quint64 qcMixSeed(quint64 seed, quint64 salt);
// Stable salt from a definition name ("qtcraft:oak_tree"; FNV-1a of the UTF-8 bytes), so the seeds of
// features and ores do not shift when content is added or removed.
quint64 qcNameSalt(const QString &name);
// Uniform value in [0, 1) for an integer position.
double qcPositionRandom(quint64 seed, int x, int y, int z);
