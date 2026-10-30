// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <QtGlobal>

#include <array>

/*! \brief Seeded gradient noise (improved Perlin) in 2D and 3D with fractal sums.

    Output of noise2/noise3 lies roughly in [-1, 1]; identical seeds give identical values on
    every platform. */
class QcNoise
{
public:
    explicit QcNoise(quint64 seed);

    /*! Returns the 2D noise at \a x, \a z. */
    double noise2(double x, double z) const;
    /*! Returns the 3D noise at \a x, \a y, \a z. */
    double noise3(double x, double y, double z) const;

    /*! Returns fractal Brownian motion of noise2() at \a x, \a z: \a octaves layers, each with
        doubled frequency and \a persistence times the amplitude, divided by the sum of the
        amplitudes (0 for no octave). */
    double fractal2(double x, double z, int octaves, double persistence = 0.5) const;
    /*! Returns fractal Brownian motion of noise3() at \a x, \a y, \a z, built like fractal2()
        from \a octaves layers with \a persistence. */
    double fractal3(double x, double y, double z, int octaves, double persistence = 0.5) const;

private:
    std::array<quint8, 512> m_permutation{};
};

/*! Returns a sub-seed mixed from \a seed and \a salt (stateless 64-bit mixing). */
quint64 qcMixSeed(quint64 seed, quint64 salt);
/*! Returns a stable salt from the definition \a name ("qtcraft:oak_tree"; FNV-1a of the UTF-8
    bytes), so the seeds of features and ores do not shift when content is added or removed. */
quint64 qcNameSalt(const QString &name);
/*! Returns a uniform value in [0, 1) for the integer position \a x, \a y, \a z and \a seed. */
double qcPositionRandom(quint64 seed, int x, int y, int z);
