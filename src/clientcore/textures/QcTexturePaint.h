// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTextureSpec.h"

#include <QColor>

#include <algorithm>
#include <cmath>

// Pixel helpers shared by the texture patterns of QcTextureSynthesizer.
namespace QcTexturePaint {

inline constexpr int size = 16;

inline QColor clear()
{
    return QColor(0, 0, 0, 0);
}

inline quint32 hashPixel(quint32 seed, int x, int y)
{
    quint32 h = seed * 0x9E3779B1u ^ static_cast<quint32>(x) * 0x85EBCA77u ^ static_cast<quint32>(y) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

// Uniform value in [-1, 1].
inline double noise(quint32 seed, int x, int y)
{
    return static_cast<double>(hashPixel(seed, x, y) & 0xFFFFu) / 32767.5 - 1.0;
}

inline QColor scaled(const QColor &color, double factor, int alpha = 255)
{
    const auto channel = [factor](int value) { return std::clamp(static_cast<int>(std::lround(value * factor)), 0, 255); };
    return QColor(channel(color.red()), channel(color.green()), channel(color.blue()), alpha);
}

inline QColor varied(const QcTextureSpec &spec, const QColor &color, int x, int y, quint32 salt = 0)
{
    return scaled(color, 1.0 + spec.variation * noise(spec.seed + salt, x, y));
}

// Colour of the pixel (x, y) of a pattern; `color` is the varied primary colour every pattern
// starts from.
using Painter = QColor (*)(const QcTextureSpec &spec, int x, int y, QColor color);

} // namespace QcTexturePaint
