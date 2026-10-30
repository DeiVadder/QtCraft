// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>

/*! \brief Small interpolation helpers of the terrain generator, inline so they stay in the hot
    loops. */
namespace QcTerrainMath {

/*! Returns the smoothstep of \a x between \a edge0 and \a edge1: 0 at or below \a edge0, 1 at
    or above \a edge1, a cubic Hermite curve between. */
inline double smoothstep(double edge0, double edge1, double x)
{
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace QcTerrainMath
