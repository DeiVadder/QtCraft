// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/icons/QcMobIcon.h"

#include "clientcore/debug/QcClientLogging.h"
#include "clientcore/entitymeshes/QcEntityMesher.h"
#include "clientcore/figures/QcEntityPose.h"
#include "clientcore/icons/QcBlockIcon.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"

#include <cmath>
#include <optional>

namespace QcMobIcon {

namespace {

using Quad = QcBlockIcon::FreeQuad;
using Corner = std::array<float, 3>;

// Creatures face north (QcEntityMesher::appendParts and appendModel). A little more than half a
// turn shows a box creature's face on the icon's south side and its long flank on the east side;
// a head turned by exactly half a turn shows its face on the south side and fills the icon like a
// block (a head turned askew would draw smaller).
constexpr double bodyYaw = 200.0;
constexpr double headYaw = 180.0;

// Skeleton creatures (people, golems) are tall: their head alone fills the square icon, and the face
// is what tells them apart.
constexpr quint8 belowHead = QcEntityMesher::boneBit(QcBoneRole::Body) | QcEntityMesher::boneBit(QcBoneRole::RightArm)
                             | QcEntityMesher::boneBit(QcBoneRole::LeftArm) | QcEntityMesher::boneBit(QcBoneRole::RightLeg)
                             | QcEntityMesher::boneBit(QcBoneRole::LeftLeg);

Corner cornerOf(const QcEntityVertex &vertex)
{
    return {vertex.x, vertex.y, vertex.z};
}

// The block face a quad looks out of: its normal's longest axis; none for a quad without area.
std::optional<QcBlockFace> faceOf(const std::array<Corner, 4> &corners)
{
    const Corner a{corners[1][0] - corners[0][0], corners[1][1] - corners[0][1], corners[1][2] - corners[0][2]};
    const Corner b{corners[3][0] - corners[0][0], corners[3][1] - corners[0][1], corners[3][2] - corners[0][2]};
    const Corner normal{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    const float x = std::abs(normal[0]);
    const float y = std::abs(normal[1]);
    const float z = std::abs(normal[2]);
    if (x + y + z <= 0.0f) {
        return std::nullopt;
    }
    if (y >= x && y >= z) {
        return normal[1] > 0.0f ? QcBlockFace::Up : QcBlockFace::Down;
    }
    if (x >= z) {
        return normal[0] > 0.0f ? QcBlockFace::East : QcBlockFace::West;
    }
    return normal[2] > 0.0f ? QcBlockFace::South : QcBlockFace::North;
}

// The quads of a mesh built from QcEntityMesh::addQuad: six indices each, the triangles a b c and
// a c d. Every mesher used here (appendParts, appendModel without a held item) adds only such quads
// and no back sides; a quad without area is left out.
QList<Quad> quadsOf(const QcEntityMesh &mesh)
{
    QList<Quad> quads;
    for (qsizetype i = 0; i + 5 < mesh.indices.size(); i += 6) {
        const std::array<quint32, 4> index{mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2], mesh.indices[i + 5]};
        Quad free;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const QcEntityVertex &vertex = mesh.vertices[index[corner]];
            free.quad.corners[corner] = cornerOf(vertex);
            free.quad.uvs[corner] = {vertex.u, vertex.v};
        }
        const std::optional<QcBlockFace> face = faceOf(free.quad.corners);
        if (!face) {
            continue;
        }
        free.quad.face = *face;
        free.tint = mesh.vertices[index[0]].tint;
        quads.append(free);
    }
    return quads;
}

// The look of a creature drawn as a skeleton model; invalid for a box creature.
QcEntityMesher::ModelLook modelLook(const QcContentRegistry &content, const QcMobDefinition &mob)
{
    QcEntityMesher::ModelLook look;
    if (mob.skeleton.isEmpty()) {
        return look;
    }
    look.model = content.assets().model(mob.skeleton);
    look.outfit = content.assets().appearance(mob.outfit);
    look.scale = mob.height / QcEntityMesher::modelHeight;
    return look;
}

QcEntityMesh meshOf(const QcContentRegistry &content, const QcMobDefinition &mob, const QcTextureAtlas &atlas)
{
    QcEntityMesh mesh;
    const QcEntityMesher::UvLookup uv = [&atlas](const QString &texture) { return atlas.uv(texture); };
    const QcEntityMesher::ModelLook look = modelLook(content, mob);
    QcEntityMesher::Placement placement{{}, bodyYaw, QcEntityLight(1.0f, 0.0f), false};
    if (look.isValid()) {
        placement.yaw = headYaw;
        QcEntityPose pose;
        pose.armsForward = mob.armsForward;
        QcEntityMesher::appendModel(mesh, {look, nullptr, nullptr, belowHead}, QcAnimator::animateBody(pose, 0.0), placement, uv);
    } else if (!mob.parts.isEmpty()) {
        QcEntityMesher::appendParts(mesh, mob.parts, placement, uv);
    }
    return mesh;
}

} // namespace

QImage render(const QcContentRegistry &content, const QcMobDefinition &mob, const QcTextureAtlas &atlas,
              const std::array<QImage, 6> &shadedAtlas, int size)
{
    const QList<Quad> quads = quadsOf(meshOf(content, mob, atlas));
    if (quads.isEmpty()) {
        qCWarning(lcIcons) << "creature" << mob.name << "has no picture: no boxes, or its skeleton" << mob.skeleton << "or outfit"
                           << mob.outfit << "is unknown";
        QImage empty(size, size, QImage::Format_RGBA8888);
        empty.fill(Qt::transparent);
        return empty;
    }
    return QcBlockIcon::renderFitted(quads, shadedAtlas, size);
}

} // namespace QcMobIcon
