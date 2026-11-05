// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/materials/QcMaterialSynthesizer.h"

#include <QImage>

/*! \brief The next mip level of the material atlases (decision E-908): every 2 x 2 block of
    texels into one. */
namespace QcMaterialMips {

/*! Returns the next level of \a normal and \a specular. Normals are decoded, averaged and
    normalised again (occlusion and height averaged); where the four normals spread, the averaged
    vector is shorter, and the smoothness of the specular texel is lowered by Toksvig's rule
    (roughness^2 + (1 - length) / length), so bumpy surfaces do not turn glossy in the distance.
    Reflectance and porosity are averaged; a metal code wins over a dielectric value when two or
    more of the four texels are metal (codes are not averaged). Emission is averaged as light (0
    is none in the atlas). */
[[nodiscard]] QcMaterialSynthesizer::Maps halve(const QImage &normal, const QImage &specular);

/*! Returns the single normal map \a normal (a pack's \c {_n}) at \a size x \a size: halved with
    halve() (channels apart, normals normalised again; never QImage's smooth scaling, which mixes
    premultiplied channels and loses them where alpha is low) while larger than \a size, repeated
    texel by texel when smaller. */
[[nodiscard]] QImage normalAt(const QImage &normal, int size);
/*! Returns the single specular map \a specular (a pack's \c {_s}) at \a size x \a size, scaled
    like normalAt(). */
[[nodiscard]] QImage specularAt(const QImage &specular, int size);

} // namespace QcMaterialMips
