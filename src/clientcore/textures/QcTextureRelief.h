// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTexturePaint.h"

#include <QImage>
#include <QPointF>

/*! \brief The relief patterns (decision E-1043): materials described as a surface with heights
    and colours at any point of the tile, not as 16 x 16 pixels. The light falls on the heights
    from the top left, and an HD texture is painted at its own resolution, so a thin crack stays
    a thin crack instead of a pixel enlarged. */
namespace QcTextureRelief {

/*! \brief A point of a material: its height in pixels of the tile and its colour before the
    light. \qcfields */
struct Surface
{
    double height = 0.0;
    QColor colour;
};

/*! The surface of a pattern at a point of the tile (pixels of the 16 x 16 tile). */
using Painter = Surface (*)(const QcTextureSpec &spec, QPointF point);

/*! \brief A pattern and its surface, a row of the tables of the pattern files. \qcfields */
struct Entry
{
    QcTextureSpec::Pattern pattern;
    Painter painter;
};

/*! Returns whether \a pattern is a relief pattern. */
[[nodiscard]] bool isRelief(QcTextureSpec::Pattern pattern);

/*! Returns the surface of \a spec, whose pattern is a relief pattern, at \a point. */
[[nodiscard]] Surface surfaceAt(const QcTextureSpec &spec, QPointF point);

/*! Returns the texture of \a spec, whose pattern is a relief pattern, with edges of
    \a resolution pixels. */
[[nodiscard]] QImage paint(const QcTextureSpec &spec, int resolution);

} // namespace QcTextureRelief
