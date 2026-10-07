#include "QcEntityMesher.h"

#include <QtMath>

#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace QcEntityMesher {

namespace {

// Same corner order as the block mesher: counter-clockwise seen from outside, corners 0 and 1 at
// the bottom edge of the texture. Indexed by QcBlockFace.
constexpr std::array<std::array<std::array<int, 3>, 4>, 6> faceCorners = {{
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}},
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}},
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}},
}};
constexpr std::array<float, 6> faceShade = {0.65f, 0.65f, 1.0f, 0.5f, 0.8f, 0.8f};
// Hurt entities flash darker for a few ticks.
constexpr float hurtShade = 0.45f;

float lerpAngle(float from, float to, double alpha)
{
    const float delta = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
    return from + delta * float(alpha);
}

} // namespace

const QList<QcMobPart> &playerModel()
{
    static const QList<QcMobPart> model = {
        {{-0.25, 0.0, -0.125}, {0.0, 0.75, 0.125}, u"qtcraft:zombie_pants"_s},
        {{0.0, 0.0, -0.125}, {0.25, 0.75, 0.125}, u"qtcraft:zombie_pants"_s},
        {{-0.25, 0.75, -0.125}, {0.25, 1.45, 0.125}, u"qtcraft:oak_planks"_s},
        {{-0.45, 0.8, -0.1}, {-0.25, 1.45, 0.1}, u"qtcraft:pig_skin"_s},
        {{0.25, 0.8, -0.1}, {0.45, 1.45, 0.1}, u"qtcraft:pig_skin"_s},
        {{-0.25, 1.45, -0.25}, {0.25, 1.8, 0.25}, u"qtcraft:pig_skin"_s},
    };
    return model;
}

void appendEntity(QcChunkMesh &mesh, const QcClientEntity &entity, const QList<QcMobPart> &parts, double alpha, float light,
                  const UvLookup &uv)
{
    const QcVec3 position = entity.previousPosition + (entity.position - entity.previousPosition) * alpha;
    const double yaw = qDegreesToRadians(double(lerpAngle(entity.previousYaw, entity.yaw, alpha)));
    const double cosYaw = std::cos(yaw);
    const double sinYaw = std::sin(yaw);
    const float tint = entity.hurtTicks > 0 ? hurtShade : 1.0f;
    for (const QcMobPart &part : parts) {
        const QRectF rect = uv(part.texture);
        for (std::size_t face = 0; face < faceCorners.size(); ++face) {
            const auto base = quint32(mesh.opaqueVertices.size());
            for (std::size_t corner = 0; corner < 4; ++corner) {
                const auto &c = faceCorners[face][corner];
                const double x = c[0] != 0 ? part.to.x : part.from.x;
                const double y = c[1] != 0 ? part.to.y : part.from.y;
                const double z = c[2] != 0 ? part.to.z : part.from.z;
                // Rotate the model (facing -Z) by yaw: yaw 90 faces +X.
                const double rx = x * cosYaw - z * sinYaw;
                const double rz = x * sinYaw + z * cosYaw;
                QcBlockVertex vertex;
                vertex.x = float(position.x + rx);
                vertex.y = float(position.y + y);
                vertex.z = float(position.z + rz);
                vertex.u = float((corner == 0 || corner == 3) ? rect.left() : rect.right());
                vertex.v = float(corner < 2 ? rect.bottom() : rect.top());
                vertex.shade = faceShade[face] * tint;
                vertex.skyLight = light;
                mesh.opaqueVertices.append(vertex);
            }
            mesh.opaqueIndices.append({base, base + 1, base + 2, base, base + 2, base + 3});
        }
    }
}

} // namespace QcEntityMesher
