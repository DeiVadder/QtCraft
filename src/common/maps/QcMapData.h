// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcWorldConstants.h"

#include <QList>
#include <QPoint>
#include <QRect>

#include <optional>

// The picture of one map (decision E-799), the same for the server, which reveals and saves it, and
// the client, which keeps what the server sent: size x size pixels over a square of the world that
// lies on a fixed grid of its scale (the map of a block column is the same for every holder), each
// pixel the top block of its column with a shade (QcMapPixel), 0 where nobody has looked yet. One
// pixel covers blocksPerPixel() x blocksPerPixel() columns; north is up (-z), east right (+x).
class QcMapData
{
public:
    static constexpr int size = 128;
    static constexpr int pixelCount = size * size;
    // Scale 0 shows one block per pixel, every step doubles it (zooming out, decision E-804).
    static constexpr int maxScale = 4;
    // Centres further out than this are damaged data: the centre of the largest map around the
    // farthest block column of the world (decision E-883) lies less than its width beyond it.
    static constexpr int maxCentre = QcWorld::maxChunkBlock + (size << maxScale);

    QcMapData() = default;
    // The blank map of `scale` whose grid square holds the block column (x, z).
    [[nodiscard]] static QcMapData around(int x, int z, int scale);
    // A map read from a save or a message: std::nullopt for a scale or centre out of range or a
    // pixel list of the wrong length.
    [[nodiscard]] static std::optional<QcMapData> fromParts(int scale, QPoint centre, QList<quint16> pixels);

    int scale() const { return m_scale; }
    // The block column at the middle of the map (the corner of the four middle pixels).
    QPoint centre() const { return m_centre; }
    int blocksPerPixel() const { return 1 << m_scale; }
    // The pixel of the block column (x, z), or std::nullopt outside the map.
    std::optional<QPoint> pixelAt(int x, int z) const;
    // The first block column of the pixel (its north west corner).
    QPoint columnOf(QPoint pixel) const;
    // Where (x, z) lies in half pixels from the map's middle, as markers count (may lie outside).
    QPointF offsetOf(double x, double z) const;

    quint16 at(QPoint pixel) const { return m_pixels[pixel.y() * size + pixel.x()]; }
    // Sets one pixel; whether it changed.
    bool set(QPoint pixel, quint16 value);
    const QList<quint16> &pixels() const { return m_pixels; }
    // The pixels of `area` row by row (area within the map).
    [[nodiscard]] QList<quint16> pixelsIn(QRect area) const;
    // Writes `values` (row by row) into `area`; false, unchanged, when they do not fit.
    bool setPixels(QRect area, const QList<quint16> &values);
    // How many pixels show something.
    int revealedCount() const;

    static QRect bounds() { return {0, 0, size, size}; }

    bool operator==(const QcMapData &) const = default;

private:
    int m_scale = 0;
    QPoint m_centre;
    QList<quint16> m_pixels = QList<quint16>(pixelCount, 0);
};
