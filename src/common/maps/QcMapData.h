// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcWorldConstants.h"

#include <QList>
#include <QPoint>
#include <QRect>

#include <optional>

/*! \brief The picture of one map (decision E-799), the same for the server, which reveals and saves
    it, and the client, which keeps what the server sent: size x size pixels over a square of the
    world that lies on a fixed grid of its scale (the map of a block column is the same for every
    holder), each pixel the top block of its column with a shade (QcMapPixel), 0 where nobody has
    looked yet.

    One pixel covers blocksPerPixel() x blocksPerPixel() columns; north is up (-z), east right
    (+x). */
class QcMapData
{
public:
    /*! Pixels along each side. */
    static constexpr int size = 128;
    /*! Pixels in all. */
    static constexpr int pixelCount = size * size;
    /*! Scale 0 shows one block per pixel, every step doubles it (zooming out, decision E-804). */
    static constexpr int maxScale = 4;
    /*! Centres further out than this are damaged data: the centre of the largest map around the
        farthest block column of the world (decision E-883) lies less than its width beyond it. */
    static constexpr int maxCentre = QcWorld::maxChunkBlock + (size << maxScale);

    QcMapData() = default;
    /*! Returns the blank map of \a scale whose grid square holds the block column (\a x, \a z). */
    [[nodiscard]] static QcMapData around(int x, int z, int scale);
    /*! Returns the map of \a scale with its middle at \a centre and \a pixels, read from a save
        or a message: std::nullopt for a scale or centre out of range or a pixel list of the wrong
        length. */
    [[nodiscard]] static std::optional<QcMapData> fromParts(int scale, QPoint centre, QList<quint16> pixels);

    /*! Returns the scale (0 to maxScale). */
    int scale() const { return m_scale; }
    /*! Returns the block column at the middle of the map (the corner of the four middle pixels). */
    QPoint centre() const { return m_centre; }
    /*! Returns the block columns along each side of one pixel: 2^scale. */
    int blocksPerPixel() const { return 1 << m_scale; }
    /*! Returns the pixel of the block column (\a x, \a z), or std::nullopt outside the map. */
    std::optional<QPoint> pixelAt(int x, int z) const;
    /*! Returns the first block column of \a pixel (its north west corner). */
    QPoint columnOf(QPoint pixel) const;
    /*! Returns where (\a x, \a z) lies in half pixels from the map's middle, as markers count (may
        lie outside). */
    QPointF offsetOf(double x, double z) const;

    /*! Returns the value of \a pixel (QcMapPixel); \a pixel must lie within the map. */
    quint16 at(QPoint pixel) const { return m_pixels[pixel.y() * size + pixel.x()]; }
    /*! Sets \a pixel to \a value. Returns whether it changed. */
    bool set(QPoint pixel, quint16 value);
    /*! Returns every pixel, row by row. */
    const QList<quint16> &pixels() const { return m_pixels; }
    /*! Returns the pixels of \a area row by row (area within the map). */
    [[nodiscard]] QList<quint16> pixelsIn(QRect area) const;
    /*! Writes \a values (row by row) into \a area. Returns false, with no change, when they do not
        fit. */
    bool setPixels(QRect area, const QList<quint16> &values);
    /*! How many pixels show something. */
    int revealedCount() const;

    /*! Returns the rectangle of all pixels. */
    static QRect bounds() { return {0, 0, size, size}; }

    bool operator==(const QcMapData &) const = default;

private:
    int m_scale = 0;
    QPoint m_centre;
    QList<quint16> m_pixels = QList<quint16>(pixelCount, 0);
};
