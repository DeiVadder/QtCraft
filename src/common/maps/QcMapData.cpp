// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/maps/QcMapData.h"

#include <algorithm>
#include <cmath>

namespace {

// Rounds down for negative numbers too.
int floorDiv(int value, int divisor)
{
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

} // namespace

QcMapData QcMapData::around(int x, int z, int scale)
{
    QcMapData map;
    map.m_scale = std::clamp(scale, 0, maxScale);
    // Squares of `width` columns, shifted by half a scale-0 map like the reference, so the first
    // map of a world has the spawn in its middle.
    const int width = size << map.m_scale;
    const int half = size / 2;
    map.m_centre = {floorDiv(x + half, width) * width + width / 2 - half, floorDiv(z + half, width) * width + width / 2 - half};
    return map;
}

std::optional<QcMapData> QcMapData::fromParts(int scale, QPoint centre, QList<quint16> pixels)
{
    if (scale < 0 || scale > maxScale || std::abs(centre.x()) > maxCentre || std::abs(centre.y()) > maxCentre || pixels.size() != pixelCount) {
        return std::nullopt;
    }
    QcMapData map;
    map.m_scale = scale;
    map.m_centre = centre;
    map.m_pixels = std::move(pixels);
    return map;
}

std::optional<QPoint> QcMapData::pixelAt(int x, int z) const
{
    const int width = size << m_scale;
    const int left = m_centre.x() - width / 2;
    const int top = m_centre.y() - width / 2;
    if (x < left || z < top || x >= left + width || z >= top + width) {
        return std::nullopt;
    }
    return QPoint((x - left) >> m_scale, (z - top) >> m_scale);
}

QPoint QcMapData::columnOf(QPoint pixel) const
{
    const int width = size << m_scale;
    return {m_centre.x() - width / 2 + (pixel.x() << m_scale), m_centre.y() - width / 2 + (pixel.y() << m_scale)};
}

QPointF QcMapData::offsetOf(double x, double z) const
{
    return {(x - m_centre.x()) * 2.0 / blocksPerPixel(), (z - m_centre.y()) * 2.0 / blocksPerPixel()};
}

bool QcMapData::set(QPoint pixel, quint16 value)
{
    quint16 &there = m_pixels[pixel.y() * size + pixel.x()];
    if (there == value) {
        return false;
    }
    there = value;
    return true;
}

QList<quint16> QcMapData::pixelsIn(QRect area) const
{
    QList<quint16> values;
    values.reserve(static_cast<qsizetype>(area.width()) * area.height());
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            values.append(at({x, y}));
        }
    }
    return values;
}

bool QcMapData::setPixels(QRect area, const QList<quint16> &values)
{
    if (area.isEmpty() || !bounds().contains(area) || values.size() != static_cast<qsizetype>(area.width()) * area.height()) {
        return false;
    }
    qsizetype next = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            m_pixels[y * size + x] = values[next++];
        }
    }
    return true;
}

int QcMapData::revealedCount() const
{
    return static_cast<int>(std::ranges::count_if(m_pixels, [](quint16 value) { return value != 0; }));
}
