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

void appendItem(QcChunkMesh &mesh, const QcClientEntity &entity, const ItemLook &look, double alpha, float light, double seconds,
                const UvLookup &uv)
{
    const QcVec3 position = entity.previousPosition + (entity.position - entity.previousPosition) * alpha;
    // Every item spins at the same speed but starts at its own angle.
    const double yaw = qDegreesToRadians(std::fmod(seconds * 60.0 + (entity.id % 360) * 37.0, 360.0));
    const double cosYaw = std::cos(yaw);
    const double sinYaw = std::sin(yaw);
    const double bob = 0.06 + 0.05 * std::sin(seconds * 2.5 + entity.id);
    const int copies = entity.item.count >= 16 ? 3 : (entity.item.count >= 2 ? 2 : 1);
    const auto place = [&](double x, double y, double z, const QcVec3 &origin) {
        const double rx = x * cosYaw - z * sinYaw;
        const double rz = x * sinYaw + z * cosYaw;
        QcBlockVertex vertex;
        vertex.x = float(origin.x + rx);
        vertex.y = float(origin.y + y);
        vertex.z = float(origin.z + rz);
        vertex.skyLight = light;
        return vertex;
    };
    for (int copy = 0; copy < copies; ++copy) {
        const QcVec3 origin = position + QcVec3{copy * 0.07, bob + copy * 0.05, copy * -0.06};
        if (look.cube) {
            const double half = itemCubeSize * 0.5;
            for (std::size_t face = 0; face < faceCorners.size(); ++face) {
                const QRectF rect = uv(look.faces[face]);
                const auto base = quint32(mesh.opaqueVertices.size());
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    const auto &c = faceCorners[face][corner];
                    QcBlockVertex vertex = place(c[0] != 0 ? half : -half, c[1] != 0 ? itemCubeSize : 0.0, c[2] != 0 ? half : -half,
                                                origin);
                    vertex.u = float((corner == 0 || corner == 3) ? rect.left() : rect.right());
                    vertex.v = float(corner < 2 ? rect.bottom() : rect.top());
                    vertex.shade = faceShade[face];
                    mesh.opaqueVertices.append(vertex);
                }
                mesh.opaqueIndices.append({base, base + 1, base + 2, base, base + 2, base + 3});
            }
            continue;
        }
        // Two crossed quads, each with a front and a back side.
        const QRectF rect = uv(look.sprite);
        const double half = itemSpriteSize * 0.5;
        for (const auto &[dx, dz] : {std::pair{half, 0.0}, std::pair{0.0, half}}) {
            for (const bool back : {false, true}) {
                const auto base = quint32(mesh.opaqueVertices.size());
                const double side = back ? -1.0 : 1.0;
                const std::array<std::array<double, 3>, 4> corners = {{
                    {-dx * side, 0.0, -dz * side},
                    {dx * side, 0.0, dz * side},
                    {dx * side, itemSpriteSize, dz * side},
                    {-dx * side, itemSpriteSize, -dz * side},
                }};
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    QcBlockVertex vertex = place(corners[corner][0], corners[corner][1], corners[corner][2], origin);
                    vertex.u = float((corner == 0 || corner == 3) ? rect.left() : rect.right());
                    vertex.v = float(corner < 2 ? rect.bottom() : rect.top());
                    vertex.shade = 0.95f;
                    mesh.opaqueVertices.append(vertex);
                }
                mesh.opaqueIndices.append({base, base + 1, base + 2, base, base + 2, base + 3});
            }
        }
    }
}

} // namespace QcEntityMesher
