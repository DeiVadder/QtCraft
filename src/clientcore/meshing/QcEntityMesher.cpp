#include "clientcore/meshing/QcEntityMesher.h"

#include "clientcore/meshing/QcHeldItemMesher.h"
#include "clientcore/meshing/QcMeshRotation.h"
#include "common/core/QcBlockFace.h"

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
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
    return {static_cast<quint8>(a[0] * b[0] / 255), static_cast<quint8>(a[1] * b[1] / 255), static_cast<quint8>(a[2] * b[2] / 255),
        static_cast<quint8>(a[3] * b[3] / 255)};
}

Tint fromRgb(quint32 rgb)
{
    return {static_cast<quint8>((rgb >> 16) & 0xFF), static_cast<quint8>((rgb >> 8) & 0xFF), static_cast<quint8>(rgb & 0xFF), 255};
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
    case QcTint::Collar:
        // A tame creature's collar travels in its appearance's shirt colour (decision E-480).
        return fromRgb(colors.shirtColor);
    case QcTint::None:
        break;
    }
    return white;
}

// Texture rectangle, tint and light of the faces of a box.
struct Surface
{
    QRectF rect;
    Tint tint = white;
    QcEntityLight light;
};

// Emits the faces of `box` whose corners `place` maps to world space; `onlyFace` limits it to one.
template<typename Place>
void emitBox(QcEntityMesh &mesh, const QcAabb &box, const Place &place, const Surface &surface, int onlyFace = -1)
{
    const QcVec3 &from = box.min;
    const QcVec3 &to = box.max;
    const QRectF &rect = surface.rect;

    // The eight corners are placed once and shared by the faces; index x + 2 y + 4 z.
    std::array<QcVec3, 8> placed;
    for (std::size_t corner = 0; corner < placed.size(); ++corner) {
        placed[corner] = place(QcVec3{(corner & 1) != 0 ? to.x : from.x, (corner & 2) != 0 ? to.y : from.y, (corner & 4) != 0 ? to.z : from.z});
    }

    for (std::size_t face = 0; face < faceCorners.size(); ++face) {
        if (onlyFace >= 0 && std::cmp_not_equal(face, onlyFace)) {
            continue;
        }

        std::array<QcEntityVertex, 4> quad;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const auto &c = faceCorners[face][corner];
            const QcVec3 &world = placed[static_cast<std::size_t>(c[0]) + 2 * static_cast<std::size_t>(c[1]) + 4 * static_cast<std::size_t>(c[2])];
            QcEntityVertex vertex = mesh.vertexAt(world);
            vertex.u = static_cast<float>((corner == 0 || corner == 3) ? rect.left() : rect.right());
            vertex.v = static_cast<float>(corner < 2 ? rect.bottom() : rect.top());
            vertex.shade = faceShade[face];
            vertex.skyLight = surface.light.sky;
            vertex.blockLight = surface.light.block;
            vertex.tint = surface.tint;
            quad[corner] = vertex;
        }
        mesh.addQuad(quad);
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

        std::array<QcEntityVertex, 4> quad;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const QcVec3 world = place(corners[corner]);
            QcEntityVertex vertex = mesh.vertexAt(world);
            // Mirrored on the back so the picture reads the same from both sides.
            const bool left = (corner == 0 || corner == 3) != back;
            vertex.u = static_cast<float>(left ? rect.left() : rect.right());
            vertex.v = static_cast<float>(corner < 2 ? rect.bottom() : rect.top());
            vertex.shade = 0.95f;
            vertex.skyLight = light.sky;
            vertex.blockLight = light.block;
            quad[corner] = vertex;
        }
        mesh.addQuad(quad);
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
                emitBox(mesh, {local(box.min), local(box.max)}, place, {uv(look.faces[static_cast<std::size_t>(face)]), white, light}, face);
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

// Model space -> bone rotation -> whole-body pose -> scale, yaw and position. A positive body
// pitch leans forward.
class BodyPlacer
{
public:
    BodyPlacer(const QcAnimator::BodyPose &pose, const Placement &placement, double scale)
        : m_pose(pose)
        , m_bodyRotate(-pose.bodyPitch, pose.bodyRoll)
        , m_turn(placement.yaw)
        , m_position(placement.position)
        , m_scale(scale)
    {
    }

    QcVec3 operator()(const QcVec3 &modelPoint) const
    {
        QcVec3 p = m_bodyRotate(modelPoint - m_pose.bodyPivot) + m_pose.bodyPivot;
        p.y += m_pose.bodyLift;
        return m_position + m_turn(p * m_scale);
    }

    // Places points of `bone`, turned by the bone's rotation in the pose.
    auto bone(const QcModelBone &bone) const
    {
        const QcAnimator::BoneRotation rotation = m_pose.bone(bone.role);
        const Rotate rotate(rotation.pitch, rotation.roll, rotation.yaw);
        const QcVec3 pivot = bone.pivot;
        return [this, rotate, pivot](const QcVec3 &p) { return (*this)(rotate(p - pivot) + pivot); };
    }

private:
    const QcAnimator::BodyPose &m_pose;
    Rotate m_bodyRotate;
    Turn m_turn;
    QcVec3 m_position;
    double m_scale = 1.0;
};

// Emits the boxes of a model in the textures and tints of an appearance's layers; boxes without a
// layer stay invisible.
struct LayerPainter
{
    QcEntityMesh &mesh;
    const UvLookup &uv;
    const QcAppearance &colors;
    Tint overlay = white;
    QcEntityLight light;

    template<typename Place>
    void operator()(const QcAppearanceDefinition &appearance, const QList<QcModelBox> &boxes, const Place &place) const
    {
        for (const QcModelBox &box : boxes) {
            if (const QcAppearanceLayer *layer = appearance.layer(box.slot)) {
                emitBox(mesh, {box.from, box.to}, place, {uv(layer->texture), multiply(tintFor(layer->tint, colors), overlay), light});
            }
        }
    }
};

// The face index of north (-Z) in faceCorners, textured by an attachment's `front`.
constexpr int northFace = 5;

template<typename Place>
void emitAttachment(const LayerPainter &paint, const Attachment &attachment, const Place &place)
{
    const Surface surface{paint.uv(attachment.texture), paint.overlay, paint.light};
    if (attachment.front.isEmpty()) {
        emitBox(paint.mesh, attachment.box, place, surface);
        return;
    }

    for (int face = 0; face < 6; ++face) {
        const bool front = face == northFace;
        emitBox(paint.mesh, attachment.box, place, front ? Surface{paint.uv(attachment.front), paint.overlay, paint.light} : surface, face);
    }
}

bool isHidden(const Figure &figure, QcBoneRole role)
{
    return (figure.hiddenBones & boneBit(role)) != 0;
}

// In the fist at the lower end of the arm, sticking out forward.
QcVec3 gripOf(const QcModelBone &arm)
{
    double bottom = arm.pivot.y;
    for (const QcModelBox &box : arm.boxes) {
        bottom = std::min(bottom, box.from.y);
    }
    return {arm.pivot.x, bottom + 0.05, arm.pivot.z - 0.05};
}

// The arm boxes of the outfit in the first-person hand, in the arm's space with the pivot at the
// origin.
template<typename Place>
void emitHandArm(const LayerPainter &paint, const QcAppearanceDefinition &outfit, const QcModelBone &arm, const Place &place)
{
    for (const QcModelBox &box : arm.boxes) {
        if (const QcAppearanceLayer *layer = outfit.layer(box.slot)) {
            emitBox(paint.mesh, {box.from - arm.pivot, box.to - arm.pivot}, place,
                    {paint.uv(layer->texture), tintFor(layer->tint, paint.colors), paint.light});
        }
    }
}

// The arm's length below its pivot; a default length without a model arm.
double armLength(const QcModelBone *arm)
{
    if (!arm) {
        return 0.6;
    }
    double length = 0.0;
    for (const QcModelBox &box : arm->boxes) {
        length = std::max(length, arm->pivot.y - box.from.y);
    }
    return length;
}

// The empty first-person arm: the shoulder sits low on the right, a little ahead of the eyes so
// the sleeve's cuff shows; the arm points forward, slightly up and inwards, drawn smaller and lower
// than an arm holding an item (decision E-722); a swing's punch moves it forward as a whole. `toWorld`
// maps the camera's frame to world space.
void emitEmptyHand(const LayerPainter &paint, const ModelLook &look, const QcAnimator::HandPose &hand, const PlaceFunction &toWorld)
{
    const QcModelBone *arm = look.model ? boneWithRole(*look.model, QcBoneRole::RightArm) : nullptr;
    if (!arm || !look.outfit) {
        return;
    }
    const QcVec3 shoulder = emptyHandShoulder + hand.offset + QcVec3{0.0, emptyPunchRise * hand.punch, -hand.punch};
    const Rotate armRotate(108.0 + hand.pitch, 0.0);
    const Turn armTurn(-14.0 - hand.yaw);
    emitHandArm(paint, *look.outfit, *arm, [&](const QcVec3 &local) { return toWorld(shoulder + armTurn(armRotate(local * emptyHandScale))); });
}

// The first-person arm holding an item (decision E-831): the fist sits at heldGrip, low right of
// the crosshair, the forearm comes up to it from the lower right corner, and the item stands in
// the fist. The hand pose turns arm and item together about the shoulder; during a swing the item
// also turns about the grip towards its swing placement.
void emitHeldHand(const LayerPainter &paint, const Figure &figure, const QcAnimator::HandPose &hand, const PlaceFunction &toWorld)
{
    const ModelLook &look = figure.look;
    const QcModelBone *arm = look.model ? boneWithRole(*look.model, QcBoneRole::RightArm) : nullptr;
    const Rotate armPitch(heldArmPitch, 0.0);
    const Turn armTurn(heldArmYaw);
    const auto armRest = [&](const QcVec3 &local) { return armTurn(armPitch(local * heldArmScale)); };
    // Points at rest relative to the shoulder; the grip lies inside the fist, above the arm's end.
    const QcVec3 grip = armRest({0.0, heldFistInset - armLength(arm), 0.0});
    const QcVec3 shoulder = heldGrip - grip + hand.offset + heldStrikeShift * hand.reach;
    const Rotate swingPitch(hand.pitch, 0.0);
    const Turn swingTurn(-hand.yaw);
    // The roll turns the hand about the fist, in the picture's plane: the tool's head swings down
    // and to the left while the fist stays in the corner.
    const Rotate swingRoll(0.0, hand.roll);
    const auto camera = [&](const QcVec3 &rest) { return toWorld(shoulder + swingTurn(swingPitch(grip + swingRoll(rest - grip)))); };

    if (arm && look.outfit) {
        emitHandArm(paint, *look.outfit, *arm, [&](const QcVec3 &local) { return camera(armRest(local)); });
    }
    const HeldItemPainter item(paint.mesh, paint.uv, paint.light);
    const HeldPlacement placement = figure.held->firstPerson.towards(figure.held->swing, hand.swing);
    item.paint(*figure.held, placement, [&](const QcVec3 &p) { return camera(grip + p); });
}

// The faces of a cape's textures (QcCapeShape): the outer side shows the emblem on the upper part
// and "outside" below; missing layers fall back to "outside" (emblem, inside) and inside (edge).
struct CapeSurfaces
{
    Surface emblem;
    Surface outside;
    Surface inside;
    Surface edge;
};

[[nodiscard]] std::optional<CapeSurfaces> capeSurfaces(const LayerPainter &paint, const QcAppearanceDefinition &cape)
{
    const QcAppearanceLayer *outside = cape.layer(u"outside"_s);
    if (!outside) {
        return std::nullopt;
    }

    const QcAppearanceLayer *emblem = cape.layer(u"emblem"_s);
    const QcAppearanceLayer *inside = cape.layer(u"inside"_s);
    const QcAppearanceLayer *edge = cape.layer(u"edge"_s);
    inside = inside ? inside : outside;
    const auto surface = [&paint](const QcAppearanceLayer *layer) {
        return Surface{paint.uv(layer->texture), multiply(tintFor(layer->tint, paint.colors), paint.overlay), paint.light};
    };
    return CapeSurfaces{surface(emblem ? emblem : outside), surface(outside), surface(inside), surface(edge ? edge : inside)};
}

// A straight part of a cape in its own frame: x across, y down from the hinge at 0, z from the
// side towards the back (0) outwards. Only its outer ends get end faces.
struct CapePiece
{
    QcAabb box;
    const Surface *outer = nullptr;
    bool top = false;
    bool bottom = false;
};

template<typename Place>
void emitCapePiece(QcEntityMesh &mesh, const CapePiece &piece, const Place &place, const CapeSurfaces &surfaces)
{
    emitBox(mesh, piece.box, place, *piece.outer, static_cast<int>(QcBlockFace::South));
    emitBox(mesh, piece.box, place, surfaces.inside, static_cast<int>(QcBlockFace::North));
    emitBox(mesh, piece.box, place, surfaces.edge, static_cast<int>(QcBlockFace::East));
    emitBox(mesh, piece.box, place, surfaces.edge, static_cast<int>(QcBlockFace::West));
    if (piece.top) {
        emitBox(mesh, piece.box, place, surfaces.edge, static_cast<int>(QcBlockFace::Up));
    }
    if (piece.bottom) {
        emitBox(mesh, piece.box, place, surfaces.edge, static_cast<int>(QcBlockFace::Down));
    }
}

QcAabb boundsOf(const QList<QcModelBox> &boxes)
{
    QcAabb bounds{boxes.front().from, boxes.front().to};
    for (const QcModelBox &box : boxes) {
        bounds.min = {std::min(bounds.min.x, box.from.x), std::min(bounds.min.y, box.from.y), std::min(bounds.min.z, box.from.z)};
        bounds.max = {std::max(bounds.max.x, box.to.x), std::max(bounds.max.y, box.to.y), std::max(bounds.max.z, box.to.z)};
    }
    return bounds;
}

// The cape of `look` on the back of the body bone (decision E-240): an upper square part with the
// emblem hinged at the top of the back, and the rest hinged below it, bending a little further.
void emitCape(const LayerPainter &paint, const ModelLook &look, const QcAnimator::BodyPose &pose, const BodyPlacer &toWorld)
{
    const QcModelBone *body = boneWithRole(*look.model, QcBoneRole::Body);
    const std::optional<CapeSurfaces> surfaces = capeSurfaces(paint, *look.cape);
    if (!body || body->boxes.isEmpty() || !surfaces) {
        return;
    }

    const QcCapeShape &shape = look.cape->cape;
    const QcAabb torso = boundsOf(body->boxes);
    const QcVec3 hinge{(torso.min.x + torso.max.x) / 2.0, torso.max.y, torso.max.z + shape.offset + look.capeClearance};
    const double half = shape.width / 2.0;
    const double upperLength = std::min(shape.width, shape.length);
    const double lowerLength = shape.length - upperLength;
    const auto onBody = toWorld.bone(*body);

    const Rotate upperTurn(-pose.capeLift, -pose.capeSway);
    const auto upperPlace = [&](const QcVec3 &p) { return onBody(hinge + upperTurn(p)); };
    emitCapePiece(paint.mesh, {{{-half, -upperLength, 0.0}, {half, 0.0, shape.thickness}}, &surfaces->emblem, true, lowerLength <= 0.0},
                  upperPlace, *surfaces);
    if (lowerLength <= 0.0) {
        return;
    }

    const Rotate lowerTurn(-(pose.capeLift + capeBend * std::max(0.0, pose.capeLift)), -pose.capeSway);
    const QcVec3 joint = hinge + upperTurn({0.0, -upperLength, 0.0});
    const auto lowerPlace = [&](const QcVec3 &p) { return onBody(joint + lowerTurn(p)); };
    emitCapePiece(paint.mesh, {{{-half, -lowerLength, 0.0}, {half, 0.0, shape.thickness}}, &surfaces->outside, false, true}, lowerPlace,
                  *surfaces);
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

const QList<QcMobPart> &minecartParts()
{
    // An open iron tub on four wheels, as long as a block along its track (z) and as wide as its body.
    static const QString side = u"qtcraft:minecart_side"_s;
    static const QString wheel = u"qtcraft:coal_block"_s;
    static const QList<QcMobPart> model = {
        {{-0.45, 0.1, -0.6}, {0.45, 0.18, 0.6}, side},
        {{-0.49, 0.18, -0.6}, {-0.41, 0.65, 0.6}, side},
        {{0.41, 0.18, -0.6}, {0.49, 0.65, 0.6}, side},
        {{-0.41, 0.18, -0.6}, {0.41, 0.65, -0.52}, side},
        {{-0.41, 0.18, 0.52}, {0.41, 0.65, 0.6}, side},
        {{-0.47, 0.0, -0.45}, {-0.39, 0.16, -0.25}, wheel},
        {{0.39, 0.0, -0.45}, {0.47, 0.16, -0.25}, wheel},
        {{-0.47, 0.0, 0.25}, {-0.39, 0.16, 0.45}, wheel},
        {{0.39, 0.0, 0.25}, {0.47, 0.16, 0.45}, wheel},
    };
    return model;
}

void appendParts(QcEntityMesh &mesh, const QList<QcMobPart> &parts, const Placement &placement, const UvLookup &uv, const PartLook &look)
{
    const Turn turn(placement.yaw);
    const auto place = [&placement, &turn](const QcVec3 &p) { return placement.position + turn(p * placement.scale); };
    const Tint overlay = placement.hurt ? hurtTint : white;
    const bool walking = look.legs && !look.sitting && !look.legs->swings().isEmpty();
    const double stride = walking ? QcAnimator::legSwing(look.walked, look.speed, look.legs->length() * placement.scale) : 0.0;
    const double swing = walking ? stride * look.legs->maxSwing() / QcAnimator::maxLegSwing : 0.0;
    for (qsizetype index = 0; index < parts.size(); ++index) {
        const QcMobPart &part = parts.at(index);
        const QcVec3 offset = look.sitting ? part.sitOffset : QcVec3{};
        const Surface surface{uv(part.texture), multiply(tintFor(part.tint, look.colors), overlay), placement.light};
        if (walking && index < look.legs->swings().size()) {
            const QcBoxLegs::Swing &leg = look.legs->swings().at(index);
            emitBox(mesh, {part.from, part.to}, [&](const QcVec3 &p) { return place(leg.apply(p, swing) + offset); }, surface);
        } else {
            emitBox(mesh, {part.from + offset, part.to + offset}, place, surface);
        }
    }
}

void appendModel(QcEntityMesh &mesh, const Figure &figure, const QcAnimator::BodyPose &pose, const Placement &placement, const UvLookup &uv)
{
    const ModelLook &look = figure.look;
    if (!look.isValid()) {
        return;
    }
    const BodyPlacer toWorld(pose, placement, look.scale);
    const LayerPainter paint{mesh, uv, look.colors, placement.hurt ? hurtTint : white, placement.light};

    for (const QcModelBone &bone : look.model->bones) {
        if (!isHidden(figure, bone.role)) {
            paint(*look.outfit, bone.boxes, toWorld.bone(bone));
        }
    }
    const QcModelBone *head = boneWithRole(*look.model, QcBoneRole::Head);
    if (look.hair && head) {
        paint(*look.hair, look.hair->boxes, toWorld.bone(*head));
    }
    if (look.cape) {
        emitCape(paint, look, pose, toWorld);
    }

    if (figure.attachments) {
        for (const Attachment &attachment : *figure.attachments) {
            const QcModelBone *bone = boneWithRole(*look.model, attachment.bone);
            if (isHidden(figure, attachment.bone)) {
                continue;
            }
            if (bone) {
                emitAttachment(paint, attachment, toWorld.bone(*bone));
            } else {
                emitAttachment(paint, attachment, toWorld);
            }
        }
    }

    const QcModelBone *arm = boneWithRole(*look.model, QcBoneRole::RightArm);
    if (figure.held && arm) {
        const QcVec3 grip = gripOf(*arm);
        const auto armPlace = toWorld.bone(*arm);
        const HeldItemPainter item(mesh, uv, placement.light);
        item.paint(*figure.held, figure.held->thirdPerson, [&](const QcVec3 &p) { return armPlace(grip + p); });
    }
}

void appendItem(QcEntityMesh &mesh, const QcClientEntity &entity, const ItemLook &look, const ItemFrame &frame, const UvLookup &uv)
{
    const QcVec3 position = entity.previousPosition + (entity.position - entity.previousPosition) * frame.alpha;
    // Every item spins at the same speed but starts at its own angle.
    const double yaw = std::fmod(frame.seconds * 60.0 + (entity.id % 360) * 37.0, 360.0);
    const double bob = 0.06 + 0.05 * std::sin(frame.seconds * 2.5 + entity.id);
    const int copies = entity.item.count >= 16 ? 3 : (entity.item.count >= 2 ? 2 : 1);

    for (int copy = 0; copy < copies; ++copy) {
        const QcVec3 origin = position + QcVec3{copy * 0.07, bob + copy * 0.05, copy * -0.06};
        if (look.cube) {
            const QcVec3 centre{0.0, itemCubeSize * 0.5, 0.0};
            const Turn turn(yaw);
            emitItem(mesh, look, [&](const QcVec3 &p) { return origin + turn(p + centre); }, frame.light, uv);
            continue;
        }

        // Two crossed sprites so the item shows from every side.
        for (const double angle : {0.0, 90.0}) {
            const Turn turn(yaw + angle);
            emitSprite(mesh, itemSpriteSize, [&](const QcVec3 &p) { return origin + turn(p); }, uv(look.sprite), glowing(frame.light, look));
        }
    }
}

void appendLyingItem(QcEntityMesh &mesh, const ItemLook &look, const LyingPlacement &placement, const UvLookup &uv)
{
    const Turn turn(placement.yaw);
    if (look.cube) {
        const QcVec3 centre{0.0, itemCubeSize * 0.5, 0.0};
        emitItem(mesh, look, [&](const QcVec3 &p) { return placement.position + turn(p + centre); }, placement.light, uv);
        return;
    }

    // The sprite's upright plane laid down: its top points north, a hair above the surface.
    constexpr double lift = 0.01;
    const auto lay = [&](const QcVec3 &p) { return placement.position + turn(QcVec3{p.x, lift, itemSpriteSize * 0.5 - p.y}); };
    emitSprite(mesh, itemSpriteSize, lay, uv(look.sprite), glowing(placement.light, look));
}

void appendOrb(QcEntityMesh &mesh, const QcClientEntity &entity, const ItemFrame &frame, const UvLookup &uv)
{
    // From 0.2 m for one point up to half a metre for the biggest orbs.
    const double size = std::clamp(0.2 + 0.03 * std::log2(std::max(1.0, entity.health)), 0.2, 0.5);
    const QcVec3 position = entity.previousPosition + (entity.position - entity.previousPosition) * frame.alpha;
    const double yaw = std::fmod(frame.seconds * 140.0 + (entity.id % 360) * 37.0, 360.0);
    const double bob = 0.05 + 0.04 * std::sin(frame.seconds * 5.0 + entity.id);
    const QcVec3 origin = position + QcVec3{0.0, bob, 0.0};
    // Orbs shine by themselves, like a torch.
    const QcEntityLight light{frame.light.sky, 1.0f};

    for (const double angle : {0.0, 90.0}) {
        const Turn turn(yaw + angle);
        emitSprite(mesh, size, [&](const QcVec3 &p) { return origin + turn(p); }, uv(QString::fromLatin1(orbTexture)), light);
    }
}

void appendBlock(QcEntityMesh &mesh, const ItemLook &look, const BlockPlacement &placement, QcEntityLight light, const UvLookup &uv)
{
    // The item cube scaled up to a block of `size` metres, centred above the feet position.
    const double scale = placement.size / itemCubeSize;
    const QcVec3 centre = placement.position + QcVec3{0.0, placement.size * 0.5, 0.0};
    emitItem(mesh, look, [&](const QcVec3 &p) { return centre + p * scale; }, light, uv);
}

void appendBlockQuads(QcEntityMesh &mesh, const QList<QcBlockRenderTable::Quad> &quads, const BlockPlacement &placement, QcEntityLight light)
{
    // The cell (0..1) scaled to a block of `size` metres, centred above the feet position.
    const QcVec3 centre = placement.position + QcVec3{0.0, placement.size * 0.5, 0.0};
    for (const QcBlockRenderTable::Quad &shape : quads) {
        std::array<QcEntityVertex, 4> quad;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const std::array<float, 3> &c = shape.corners[corner];
            const QcVec3 local{static_cast<double>(c[0]) - 0.5, static_cast<double>(c[1]) - 0.5, static_cast<double>(c[2]) - 0.5};
            QcEntityVertex vertex = mesh.vertexAt(centre + local * placement.size);
            vertex.u = shape.uvs[corner][0];
            vertex.v = shape.uvs[corner][1];
            vertex.shade = faceShade[static_cast<std::size_t>(shape.face)];
            vertex.skyLight = light.sky;
            vertex.blockLight = light.block;
            quad[corner] = vertex;
        }
        mesh.addQuad(quad);
    }
}

void appendPlacedItem(QcEntityMesh &mesh, const ItemLook &look, const PlaceFunction &place, QcEntityLight light, const UvLookup &uv)
{
    emitItem(mesh, look, place, light, uv);
}

void appendHand(QcEntityMesh &mesh, const Figure &figure, const QcAnimator::HandPose &hand, const HandView &view, const UvLookup &uv)
{
    const double y = qDegreesToRadians(view.yaw);
    const double p = qDegreesToRadians(view.pitch);
    const QcVec3 forward{std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
    const QcVec3 right{std::cos(y), 0.0, std::sin(y)};
    const QcVec3 up{-std::sin(y) * std::sin(p), std::cos(p), std::cos(y) * std::sin(p)};
    // Camera space (x right, y up, z backwards) -> world.
    const PlaceFunction toWorld = [&](const QcVec3 &c) { return view.eye + right * c.x + up * c.y - forward * c.z; };

    const LayerPainter paint{mesh, uv, figure.look.colors, white, view.light};
    if (figure.held) {
        emitHeldHand(paint, figure, hand, toWorld);
    } else {
        emitEmptyHand(paint, figure.look, hand, toWorld);
    }
}

} // namespace QcEntityMesher
