// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>

/*! \brief The smooth outline of an enlarged cut-out shape (decision E-1000).

    Every visible pixel of the 16 x 16 picture is a disc around its centre, joined to its visible
    neighbours by strokes as wide as a pixel: to the four direct neighbours always, to a diagonal
    neighbour where both pixels between them are clear (a step of a diagonal line, also where it
    meets another part) or where one is and the pixels beyond it are clear too (a concave corner,
    which Scale2x fills as well). The end of a band two pixels thick that hangs on one diagonal
    neighbour (a blade's tip) is joined to the band's other row, so the band tapers to a point. A
    pixel of the enlarged picture is visible when its centre lies within half a pixel of the
    16 x 16 picture from a disc or a stroke. Straight and diagonal edges stay straight at every
    resolution, thin diagonal lines keep one width, holes keep their shape; the colours of the
    16 x 16 picture's pixels inside never move the outline. The shapes are icons that never
    repeat: pixels beyond the picture's border count as clear. Integer arithmetic only, so the
    outline is the same on every architecture (like the patterns built without floating point
    contraction, decisions E-300 and E-901). */
namespace QcTextureOutline {

/*! Returns \a enlarged, the square picture \a base enlarged to a whole multiple of its size
    (a power of two in the game), as RGBA8888 cut to the outline of \a base: pixels outside it
    become transparent, pixels inside it that \a enlarged leaves transparent take the colour of
    the nearest visible pixel of \a base. \a enlarged is returned unchanged when \a base has no
    transparent pixel, is not square, or \a enlarged is not a square whole multiple of it. */
[[nodiscard]] QImage cut(const QImage &enlarged, const QImage &base);

} // namespace QcTextureOutline
