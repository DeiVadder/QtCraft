// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTexturePaint.h"

/*! Returns the colour of pixel (\a x, \a y) of the sword in the base of an enlarged texture
    (QcTextureSpec::detailed, decision E-1000): the blade of the 16 x 16 icon on the diagonal,
    pointed, with only a trace of the pixel variation; a guard across it
    and a pommel in the primary colour darkened; a wrapped grip in the secondary colour. \a spec
    gives the colours and the seed; the varied primary colour is not used. */
QColor qcPaintDetailedSword(const QcTextureSpec &spec, int x, int y, QColor);

/*! Returns the colour of pixel (\a x, \a y) of the axe in the base of an enlarged texture
    (QcTextureSpec::detailed, decision E-1000): the haft of the 16 x 16 icon on the diagonal in
    the secondary colour, through the eye of a head across it in the primary colour: a narrow
    darker cheek at the haft, a bit that flares to the top left into a long convex cutting edge,
    lit, its lower horn drawn down into a small beard, and a short poll behind the haft. \a spec
    gives the colours and the seed; the varied primary colour is not used. */
QColor qcPaintDetailedAxe(const QcTextureSpec &spec, int x, int y, QColor);
