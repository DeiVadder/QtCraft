#include "clientcore/meshing/QcFigureLook.h"

#include "clientcore/meshing/QcArmourLook.h"
#include "clientcore/meshing/QcCapeSwing.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcArmour.h"
#include "common/world/QcLightVolume.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcFigureLook {

QcEntityMesher::ModelLook player(const QcContentRegistry &content, const QcAppearance &appearance)
{
    QcEntityMesher::ModelLook look;
    look.model = content.assets().model(u"qtcraft:humanoid"_s);
    look.outfit = content.assets().appearance(appearance.outfit);
    if (!look.outfit || look.outfit->kind != QcAppearanceDefinition::Kind::Outfit) {
        look.outfit = content.assets().appearance(QcAppearance().outfit);
    }
    look.hair = content.assets().appearance(appearance.hair);
    if (look.hair && look.hair->kind != QcAppearanceDefinition::Kind::Hair) {
        look.hair = nullptr;
    }
    look.cape = cape(content, appearance.cape);
    look.colors = appearance;
    return look;
}

Dressed armour(const QcContentRegistry &content, const QcEquipment &equipment, QcEntityMesher::ModelLook look)
{
    const QcItemStack &head = equipment.at(QcEquipmentSlot::Head);
    if (!head.isEmpty() && QcArmour::of(content, head.item)) {
        look.hair = nullptr;
    }
    QList<QcEntityMesher::Attachment> attachments = QcArmourLook::attachments(content, equipment);
    look.capeClearance = look.model ? QcArmourLook::capeClearance(*look.model, attachments) : 0.0;
    return {std::move(look), std::move(attachments)};
}

QcEntityMesher::ModelLook stand(const QcContentRegistry &content, const QcStandPose &pose)
{
    QcEntityMesher::ModelLook look;
    look.model = content.assets().model(u"qtcraft:armor_stand"_s);
    look.outfit = content.assets().appearance(u"qtcraft:armor_stand"_s);
    look.scale = pose.small ? 0.5 : 1.0;
    return look;
}

Dressed standArmour(const QcContentRegistry &content, const QcEquipment &equipment, QcEntityMesher::ModelLook look)
{
    Dressed dressed = armour(content, equipment, std::move(look));
    const QcEntityMesher::ModelLook &worn = dressed.look;
    if (!worn.model || !worn.outfit) {
        return dressed;
    }

    // The filler takes the texture the outfit gives the stand's own head.
    const auto head = std::ranges::find(worn.model->bones, QcBoneRole::Head, &QcModelBone::role);
    const QcAppearanceLayer *wood = head == worn.model->bones.end() || head->boxes.isEmpty() ? nullptr : worn.outfit->layer(head->boxes.constFirst().slot);
    if (wood) {
        // Drawn before the helmet, which covers it everywhere but at the opening.
        dressed.armour = QcArmourLook::headFillers(content, equipment, wood->texture) + dressed.armour;
    }
    return dressed;
}

quint8 standHiddenBones(const QcStandPose &pose)
{
    using QcEntityMesher::boneBit;
    quint8 hidden = 0;
    if (!pose.arms) {
        hidden |= boneBit(QcBoneRole::LeftArm) | boneBit(QcBoneRole::RightArm);
    }
    if (!pose.basePlate) {
        hidden |= boneBit(QcBoneRole::None);
    }
    return hidden;
}

QcAnimator::BodyPose standBody(const QcStandPose &pose, double seconds)
{
    using Part = QcStandPose::Part;
    static constexpr std::array<std::pair<Part, QcBoneRole>, QcStandPose::partCount> roles{{
        {Part::Head, QcBoneRole::Head},
        {Part::Body, QcBoneRole::Body},
        {Part::LeftArm, QcBoneRole::LeftArm},
        {Part::RightArm, QcBoneRole::RightArm},
        {Part::LeftLeg, QcBoneRole::LeftLeg},
        {Part::RightLeg, QcBoneRole::RightLeg},
    }};

    QcAnimator::BodyPose body;
    for (const auto &[part, role] : roles) {
        const QcStandPose::Rotation &rotation = pose.at(part);
        body.bones[static_cast<std::size_t>(role)] = {rotation.x, rotation.z, rotation.y};
    }
    const QcCapeSwing::Angles cape = QcCapeSwing::target(QcCapeDrive(), seconds);
    body.capeLift = cape.lift;
    body.capeSway = cape.sway;
    return body;
}

const QcAppearanceDefinition *cape(const QcContentRegistry &content, const QString &name)
{
    const QcAppearanceDefinition *definition = name.isEmpty() ? nullptr : content.assets().appearance(name);
    return definition && definition->kind == QcAppearanceDefinition::Kind::Cape ? definition : nullptr;
}

QcBlockId itemState(const QcContentRegistry &content, QcBlockId block)
{
    if (!content.block(block).connectsTo.isEmpty()) {
        block = content.withProperty(content.withProperty(block, u"east"_s, u"true"_s), u"west"_s, u"true"_s);
    }
    return block;
}

namespace {

// The kind of held thing `definition` is, for its hold defaults (decision E-831).
QcHoldCategory categoryOf(const QcItemDefinition &definition, const QcEntityMesher::ItemLook &look)
{
    if (look.cube) {
        return look.boxes.isEmpty() ? QcHoldCategory::Block : QcHoldCategory::PartialBlock;
    }
    if (definition.launcher) {
        return QcHoldCategory::Launcher;
    }
    if (!definition.tool.isEmpty() || definition.fishing) {
        return QcHoldCategory::Tool;
    }
    return definition.attackDamage > 1.0 ? QcHoldCategory::Weapon : QcHoldCategory::Sprite;
}

QcEntityMesher::HeldPlacement placementOf(const QcHoldTransform &transform)
{
    QcEntityMesher::HeldPlacement placement;
    placement.translate = transform.translate.value_or(placement.translate);
    placement.rotate = transform.rotate.value_or(placement.rotate);
    placement.scale = transform.scale.value_or(placement.scale);
    return placement;
}

// `look` with how it is held: the item's own hold over the hold of the block it places over its
// category's defaults.
QcEntityMesher::ItemLook withHold(QcEntityMesher::ItemLook look, const QcContentRegistry &content, const QcItemDefinition &definition,
                                  const QcHoldDisplay &blockHold)
{
    const QcHoldDisplay own = definition.hold.over(blockHold);
    const QcHoldCategory category = own.category.value_or(categoryOf(definition, look));
    const QcHoldDisplay display = own.over(content.holdDefaults().of(category));
    look.grip = display.grip.value_or(QcHoldGrip::Base);
    look.firstPerson = placementOf(display.firstPerson);
    look.thirdPerson = placementOf(display.thirdPerson);
    look.swing = placementOf(display.swing.over(display.firstPerson));
    return look;
}

} // namespace

QcEntityMesher::ItemLook item(const QcContentRegistry &content, QcItemId item)
{
    QcEntityMesher::ItemLook look;
    if (!content.isValidItem(item)) {
        return look;
    }

    const QcItemDefinition &definition = content.item(item);
    look.sprite = definition.texture;
    const std::optional<QcBlockId> block = content.blockId(definition.placesBlock);
    if (block) {
        look.glow = static_cast<float>(content.block(*block).lightLevel) / static_cast<float>(QcLightVolume::maxLevel);
    }
    if (block && content.block(*block).itemTexture.isEmpty() && !content.state(*block).boxes.isEmpty()
        && content.block(*block).renderLayer != QcRenderLayer::Invisible) {
        look.cube = true;
        look.faces = content.block(*block).faceTextures;
        if (!content.state(itemState(content, *block)).fullCube) {
            look.boxes = content.state(itemState(content, *block)).boxes;
        }
    }
    return withHold(std::move(look), content, definition, block ? content.block(*block).hold : QcHoldDisplay());
}

[[nodiscard]] std::optional<QcEntityMesher::ItemLook> held(const QcContentRegistry &content, QcItemId item)
{
    if (item == qcNoItem || !content.isValidItem(item)) {
        return std::nullopt;
    }
    return QcFigureLook::item(content, item);
}

std::optional<QcEntityMesher::ItemLook> flames(const QcContentRegistry &content, double width, double height)
{
    const std::optional<QcBlockId> fire = content.defaultFire();
    if (!fire || height <= 0.0) {
        return std::nullopt;
    }

    QcEntityMesher::ItemLook look;
    look.cube = true;
    look.faces = content.block(*fire).faceTextures;
    look.glow = 1.0f;
    // Walls a little outside the body (gap metres beyond half its width, whatever its shape), as
    // thin as the mesher draws them without flicker. The block is drawn `height` metres high, so
    // the walls are given in units of the height.
    constexpr double gap = 0.06;
    constexpr double thickness = 0.01;
    const double half = (width * 0.5 + gap) / height;
    const double low = 0.5 - half;
    const double high = 0.5 + half;
    look.boxes = {{{low, 0.0, low}, {high, 1.0, low + thickness}},
                  {{low, 0.0, high - thickness}, {high, 1.0, high}},
                  {{low, 0.0, low}, {low + thickness, 1.0, high}},
                  {{high - thickness, 0.0, low}, {high, 1.0, high}}};
    return look;
}

} // namespace QcFigureLook
