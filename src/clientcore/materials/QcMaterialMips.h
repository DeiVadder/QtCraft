#pragma once

#include <QImage>

// The next mip level of the material atlases (decision E-908): every 2 x 2 block of texels into one.
namespace QcMaterialMips {

struct Level
{
    QImage normal;
    QImage specular;
};

// Normals are decoded, averaged and normalised again (occlusion and height averaged); where the
// four normals spread, the averaged vector is shorter, and the smoothness of the specular texel is
// lowered by Toksvig's rule (roughness^2 + (1 - length) / length), so bumpy surfaces do not turn
// glossy in the distance. Reflectance and porosity are averaged; a metal code wins over a
// dielectric value when two or more of the four texels are metal (codes are not averaged).
// Emission is averaged as light (0 is none in the atlas).
[[nodiscard]] Level halve(const QImage &normal, const QImage &specular);

// A single map (a pack's `_n` or `_s`) at `size` x `size`: halved with halve() (channels apart,
// normals normalised again; never QImage's smooth scaling, which mixes premultiplied channels and
// loses them where alpha is low) while larger than `size`, repeated texel by texel when smaller.
[[nodiscard]] QImage normalAt(const QImage &normal, int size);
[[nodiscard]] QImage specularAt(const QImage &specular, int size);

} // namespace QcMaterialMips
