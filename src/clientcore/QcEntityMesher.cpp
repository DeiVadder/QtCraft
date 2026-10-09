#include "QcEntityMesher.h"

#include <QtMath>

#include <array>
#include <cmath>
#include <utility>

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
using Tint = std::array<quint8, 4>;
constexpr Tint white = {255, 255, 255, 255};
// Hit entities flash red for a few ticks.
constexpr Tint hurtTint = {255, 110, 110, 255};

Tint multiply(Tint a, Tint b)
{
    return {quint8(a[0] * b[0] / 255), quint8(a[1] * b[1] / 255), quint8(a[2] * b[2] / 255), quint8(a[3] * b[3] / 255)};
}

Tint fromRgb(quint32 rgb)
{
    return {quint8((rgb >> 16) & 0xFF), quint8((rgb >> 8) & 0xFF), quint8(rgb & 0xFF), 255};
}

Tint tintFor(QcTint tint, const QcAppearance &colors)
{
    switch (tint) {
    case QcTint::Skin:
        return fromRgb(colors.skinColor);
    case QcTint::Hair:
        return fromRgb(colors.hairColor);
    case QcTint::Shirt:
        return fromRgb(colors.shirtColor);
    case QcTint::Pants:
        return fromRgb(colors.pantsColor);
    case QcTint::None:
        break;
    }
    return white;
}

// Rotation around the X axis (positive moves -Y towards -Z) and then around Z (positive moves -Y
// towards +X), both in degrees. Sine and cosine are taken once, not per vertex.
struct Rotate
{
    double cosPitch;
    double sinPitch;
    double cosRoll;
    double sinRoll;

    Rotate(double pitch, double roll)
        : cosPitch(std::cos(qDegreesToRadians(pitch)))
        , sinPitch(std::sin(qDegreesToRadians(pitch)))
        , cosRoll(std::cos(qDegreesToRadians(roll)))
        , sinRoll(std::sin(qDegreesToRadians(roll)))
    {
    }

    QcVec3 operator()(const QcVec3 &p) const
    {
        const QcVec3 q{p.x, p.y * cosPitch - p.z * sinPitch, p.y * sinPitch + p.z * cosPitch};
        return {q.x * cosRoll - q.y * sinRoll, q.x * sinRoll + q.y * cosRoll, q.z};
    }
};

// Rotation around the Y axis by a yaw in degrees: the model faces -Z; yaw 90 faces +X.
struct Turn
{
    double cosYaw;
    double sinYaw;

    explicit Turn(double yawDegrees)
        : cosYaw(std::cos(qDegreesToRadians(yawDegrees)))
        , sinYaw(std::sin(qDegreesToRadians(yawDegrees)))
    {
    }

    QcVec3 operator()(const QcVec3 &p) const { return {p.x * cosYaw - p.z * sinYaw, p.y, p.x * sinYaw + p.z * cosYaw}; }
};

// Emits the faces of a box whose corners `place` maps to world space; `onlyFace` limits it to one.
template<typename Place>
void emitBox(QcEntityMesh &mesh, const QcVec3 &from, const QcVec3 &to, const Place &place, const QRectF &rect, Tint tint, QcEntityLight light,
             int onlyFace = -1)
{
    // The eight corners are placed once and shared by the faces; index x + 2 y + 4 z.
    std::array<QcVec3, 8> placed;
    for (std::size_t corner = 0; corner < placed.size(); ++corner) {
        placed[corner] = place(QcVec3{(corner & 1) != 0 ? to.x : from.x, (corner & 2) != 0 ? to.y : from.y, (corner & 4) != 0 ? to.z : from.z});
    }
    for (std::size_t face = 0; face < faceCorners.size(); ++face) {
        if (onlyFace >= 0 && std::cmp_not_equal(face, onlyFace)) {
            continue;
        }
        const auto base = quint32(mesh.vertices.size());
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const auto &c = faceCorners[face][corner];
            const QcVec3 &world = placed[std::size_t(c[0]) + 2 * std::size_t(c[1]) + 4 * std::size_t(c[2])];
            QcEntityVertex vertex;
            vertex.x = float(world.x);
            vertex.y = float(world.y);
            vertex.z = float(world.z);
            vertex.u = float((corner == 0 || corner == 3) ? rect.left() : rect.right());
            vertex.v = float(corner < 2 ? rect.bottom() : rect.top());
            vertex.shade = faceShade[face];
            vertex.skyLight = light.sky;
            vertex.blockLight = light.block;
            vertex.tint = tint;
            mesh.vertices.append(vertex);
        }
        mesh.indices.append({base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

// A flat, double-sided square of `size` in the local XY plane, centred on x and standing on y = 0.
template<typename Place>
void emitSprite(QcEntityMesh &mesh, double size, const Place &place, const QRectF &rect, QcEntityLight light)
{
    const double half = size * 0.5;
    for (const bool back : {false, true}) {
        const double side = back ? -1.0 : 1.0;
        const std::array<QcVec3, 4> corners = {{{-half * side, 0.0, 0.0}, {half * side, 0.0, 0.0}, {half * side, size, 0.0},
                                                {-half * side, size, 0.0}}};
        const auto base = quint32(mesh.vertices.size());
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const QcVec3 world = place(corners[corner]);
            QcEntityVertex vertex;
            vertex.x = float(world.x);
            vertex.y = float(world.y);
            vertex.z = float(world.z);
            // Mirrored on the back so the picture reads the same from both sides.
            const bool left = (corner == 0 || corner == 3) != back;
            vertex.u = float(left ? rect.left() : rect.right());
            vertex.v = float(corner < 2 ? rect.bottom() : rect.top());
            vertex.shade = 0.95f;
            vertex.skyLight = light.sky;
            vertex.blockLight = light.block;
            mesh.vertices.append(vertex);
        }
        mesh.indices.append({base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

// An item whose local space `place` maps to world: cubes are centred on the origin, sprites stand
// on it.
QcEntityLight glowing(QcEntityLight light, const ItemLook &look)
{
    return {light.sky, std::max(light.block, look.glow)};
}

template<typename Place>
void emitItem(QcEntityMesh &mesh, const ItemLook &look, const Place &place, QcEntityLight around, const UvLookup &uv)
{
    const QcEntityLight light = glowing(around, look);
    if (look.cube) {
        const double half = itemCubeSize * 0.5;
        const QList<QcAabb> boxes = look.boxes.isEmpty() ? QList<QcAabb>{{{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}}} : look.boxes;
        const auto local = [half](const QcVec3 &p) { return QcVec3{p.x * 2.0 * half - half, p.y * 2.0 * half - half, p.z * 2.0 * half - half}; };
        for (const QcAabb &box : boxes) {
            for (int face = 0; face < 6; ++face) {
                emitBox(mesh, local(box.min), local(box.max), place, uv(look.faces[std::size_t(face)]), white, light, face);
            }
        }
        return;
    }
    emitSprite(mesh, itemSpriteSize, place, uv(look.sprite), light);
}

const QcModelBone *boneWithRole(const QcModelDefinition &model, QcBoneRole role)
{
    for (const QcModelBone &bone : model.bones) {
        if (bone.role == role) {
            return &bone;
        }
    }
    return nullptr;
}

} // namespace

const QList<QcMobPart> &playerParts()
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

void appendParts(QcEntityMesh &mesh, const QList<QcMobPart> &parts, const Placement &placement, const UvLookup &uv)
{
    const Turn turn(placement.yaw);
    const auto place = [&placement, &turn](const QcVec3 &p) { return placement.position + turn(p); };
    for (const QcMobPart &part : parts) {
        emitBox(mesh, part.from, part.to, place, uv(part.texture), placement.hurt ? hurtTint : white, placement.light);
    }
}

void appendModel(QcEntityMesh &mesh, const ModelLook &look, const QcAnimator::BodyPose &pose, const Placement &placement,
                 const ItemLook *held, const UvLookup &uv)
{
    if (!look.isValid()) {
        return;
    }
    const Tint overlay = placement.hurt ? hurtTint : white;
    // Model space -> bone rotation -> whole-body pose -> scale, yaw and position. A positive body
    // pitch leans forward.
    const Rotate bodyRotate(-pose.bodyPitch, pose.bodyRoll);
    const Turn turn(placement.yaw);
    const auto toWorld = [&](const QcVec3 &modelPoint) {
        QcVec3 p = bodyRotate(modelPoint - pose.bodyPivot) + pose.bodyPivot;
        p.y += pose.bodyLift;
        return placement.position + turn(p * look.scale);
    };
    const auto boneTransform = [&toWorld, &pose](const QcModelBone &bone) {
        const QcAnimator::BoneRotation rotation = pose.bone(bone.role);
        const Rotate rotate(rotation.pitch, rotation.roll);
        const QcVec3 pivot = bone.pivot;
        return [&toWorld, rotate, pivot](const QcVec3 &p) { return toWorld(rotate(p - pivot) + pivot); };
    };
    const auto emitLayered = [&](const QcAppearanceDefinition &appearance, const QList<QcModelBox> &boxes, const auto &place) {
        for (const QcModelBox &box : boxes) {
            if (const QcAppearanceLayer *layer = appearance.layer(box.slot)) {
                emitBox(mesh, box.from, box.to, place, uv(layer->texture), multiply(tintFor(layer->tint, look.colors), overlay),
                        placement.light);
            }
        }
    };
    for (const QcModelBone &bone : look.model->bones) {
        emitLayered(*look.outfit, bone.boxes, boneTransform(bone));
    }
    const QcModelBone *head = boneWithRole(*look.model, QcBoneRole::Head);
    if (look.hair && head) {
        emitLayered(*look.hair, look.hair->boxes, boneTransform(*head));
    }
    const QcModelBone *arm = boneWithRole(*look.model, QcBoneRole::RightArm);
    if (held && arm) {
        // In the fist at the lower end of the arm, sticking out forward.
        double bottom = arm->pivot.y;
        for (const QcModelBox &box : arm->boxes) {
            bottom = std::min(bottom, box.from.y);
        }
        const QcVec3 grip{arm->pivot.x, bottom + 0.05, arm->pivot.z - 0.05};
        const auto armPlace = boneTransform(*arm);
        if (held->cube) {
            emitItem(mesh, *held, [&](const QcVec3 &p) { return armPlace(grip + p * 0.7); }, placement.light, uv);
        } else {
            // Tools point forward and up out of the fist; two crossed sprites keep them visible
            // from the front and the side.
            const Rotate tilt(-45.0, 0.0);
            for (const double angle : {0.0, 90.0}) {
                const Turn spin(angle);
                const auto place = [&](const QcVec3 &p) { return armPlace(grip + tilt(spin(p * 0.9))); };
                emitSprite(mesh, itemSpriteSize, place, uv(held->sprite), glowing(placement.light, *held));
            }
        }
    }
}

void appendItem(QcEntityMesh &mesh, const QcClientEntity &entity, const ItemLook &look, double alpha, QcEntityLight light, double seconds,
                const UvLookup &uv)
{
    const QcVec3 position = entity.previousPosition + (entity.position - entity.previousPosition) * alpha;
    // Every item spins at the same speed but starts at its own angle.
    const double yaw = std::fmod(seconds * 60.0 + (entity.id % 360) * 37.0, 360.0);
    const double bob = 0.06 + 0.05 * std::sin(seconds * 2.5 + entity.id);
    const int copies = entity.item.count >= 16 ? 3 : (entity.item.count >= 2 ? 2 : 1);
    for (int copy = 0; copy < copies; ++copy) {
        const QcVec3 origin = position + QcVec3{copy * 0.07, bob + copy * 0.05, copy * -0.06};
        if (look.cube) {
            const QcVec3 centre{0.0, itemCubeSize * 0.5, 0.0};
            const Turn turn(yaw);
            emitItem(mesh, look, [&](const QcVec3 &p) { return origin + turn(p + centre); }, light, uv);
            continue;
        }
        // Two crossed sprites so the item shows from every side.
        for (const double angle : {0.0, 90.0}) {
            const Turn turn(yaw + angle);
            emitSprite(mesh, itemSpriteSize, [&](const QcVec3 &p) { return origin + turn(p); }, uv(look.sprite), glowing(light, look));
        }
    }
}

void appendBlock(QcEntityMesh &mesh, const ItemLook &look, const QcVec3 &position, QcEntityLight light, const UvLookup &uv)
{
    // The item cube scaled up to a block of 0.98 m, centred above the feet position.
    constexpr double size = 0.98;
    const double scale = size / itemCubeSize;
    const QcVec3 centre = position + QcVec3{0.0, size * 0.5, 0.0};
    emitItem(mesh, look, [&](const QcVec3 &p) { return centre + p * scale; }, light, uv);
}

void appendHand(QcEntityMesh &mesh, const ModelLook &look, const ItemLook *held, const QcAnimator::HandPose &hand, const QcVec3 &eye,
                double yaw, double pitch, QcEntityLight light, const UvLookup &uv)
{
    const double y = qDegreesToRadians(yaw);
    const double p = qDegreesToRadians(pitch);
    const QcVec3 forward{std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
    const QcVec3 right{std::cos(y), 0.0, std::sin(y)};
    const QcVec3 up{-std::sin(y) * std::sin(p), std::cos(p), std::cos(y) * std::sin(p)};
    // Camera space (x right, y up, z backwards) -> world.
    const auto toWorld = [&](const QcVec3 &c) { return eye + right * c.x + up * c.y - forward * c.z; };
    // The shoulder sits low on the right; the arm points forward, slightly up and inwards.
    const QcVec3 shoulder = QcVec3{0.4, -0.36, 0.05} + hand.offset;
    const Rotate armRotate(108.0 + hand.pitch, 0.0);
    const Turn armTurn(-14.0 - hand.yaw);
    const auto armCamera = [&](const QcVec3 &local) { return shoulder + armTurn(armRotate(local)); };
    const auto armSpace = [&](const QcVec3 &local) { return toWorld(armCamera(local)); };
    const QcModelBone *arm = look.model ? boneWithRole(*look.model, QcBoneRole::RightArm) : nullptr;
    double length = 0.6;
    if (arm && look.outfit) {
        length = 0.0;
        for (const QcModelBox &box : arm->boxes) {
            if (const QcAppearanceLayer *layer = look.outfit->layer(box.slot)) {
                emitBox(mesh, box.from - arm->pivot, box.to - arm->pivot, armSpace, uv(layer->texture),
                        tintFor(layer->tint, look.colors), light);
            }
            length = std::max(length, arm->pivot.y - box.from.y);
        }
    }
    if (held) {
        // Held in front of the fist, facing the camera: blocks turned a little to show three faces,
        // tools tilted so the blade points up and to the left.
        const QcVec3 fist = armCamera({0.0, -length, 0.0});
        if (held->cube) {
            const Turn turn(35.0);
            const Rotate rotate(-20.0, 0.0);
            const auto place = [&](const QcVec3 &local) { return toWorld(fist + QcVec3{-0.02, 0.06, -0.12} + rotate(turn(local * 0.6))); };
            emitItem(mesh, *held, place, light, uv);
        } else {
            const Rotate rotate(0.0, 35.0);
            const Turn turn(-20.0);
            const auto place = [&](const QcVec3 &local) {
                return toWorld(fist + QcVec3{-0.03, 0.04, -0.12} + turn(rotate(local * handSpriteScale)));
            };
            emitItem(mesh, *held, place, light, uv);
        }
    }
}

} // namespace QcEntityMesher
