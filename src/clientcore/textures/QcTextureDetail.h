#pragma once

#include "QcTextureSpec.h"

#include <QImage>
#include <QJsonObject>

// Renders a procedural texture above 16 x 16 (decision E-299): the pattern's 16 x 16 picture is
// enlarged in steps of two with an edge-aware pixel-art filter (Scale2x on similar colours, so
// diagonals and round shapes get finer steps instead of blurring), then gains detail that only the
// higher resolution can show: a fine grain on the surface and a bevel along strong edges (lit at
// the top and left, shaded at the bottom and right, also towards transparent pixels). How much of
// each a pattern gets is a table per pattern; a texture definition may change it with "detail".
namespace QcTextureDetail {

struct Params
{
    // Strength of the fine grain (share of the colour, 0..1).
    double grain = 0.0;
    // Strength of the bevel along edges (share of the colour, 0..1).
    double bevel = 0.0;
    // Smooth diagonal and round outlines while enlarging; off for patterns of straight lines.
    bool smooth = false;

    friend bool operator==(const Params &, const Params &) = default;
};

// The defaults of a pattern.
Params forPattern(QcTextureSpec::Pattern pattern);
// The pattern's defaults changed by the fields of a texture definition's "detail" object.
Params withOverrides(Params params, const QJsonObject &detail);

// Enlarges `base` (16 x 16) to `resolution` (a power of two from 32 to 512) and adds the detail.
// Deterministic: the same input gives the same image. Partially transparent pixels keep their
// alpha, the other pixels stay fully opaque or fully transparent.
QImage enlarge(const QImage &base, int resolution, const Params &params, quint32 seed);

} // namespace QcTextureDetail
