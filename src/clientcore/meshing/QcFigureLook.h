#pragma once

#include "clientcore/meshing/QcEntityMesher.h"
#include "common/content/definitions/QcAppearance.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcStandPose.h"

#include <optional>

class QcContentRegistry;

// What players and items look like when drawn: shared by the world (QcEntityFrameBuilder) and the
// character preview, so the preview shows exactly what other players see.
namespace QcFigureLook {

// The humanoid model with the appearance's outfit, hair and cape; an outfit the content does not
// offer falls back to the default outfit, a hair style or cape of the wrong kind to none. Not
// valid when the content has no humanoid model.
QcEntityMesher::ModelLook player(const QcContentRegistry &content, const QcAppearance &appearance);
// The cape called `name`; none for an empty name, an unknown one or another kind of appearance.
const QcAppearanceDefinition *cape(const QcContentRegistry &content, const QString &name);

// A figure's look with what it wears.
struct Dressed
{
    QcEntityMesher::ModelLook look;
    QList<QcEntityMesher::Attachment> armour;
};

// What a figure wears: the attachments of its armour (QcArmourLook) and `look` with it: under a
// helmet the hair is left out, so it does not poke through, and a cape hangs outside a chest layer
// (ModelLook::capeClearance).
[[nodiscard]] Dressed armour(const QcContentRegistry &content, const QcEquipment &equipment, QcEntityMesher::ModelLook look);

// An armour stand (decision E-238): the stand model with its wooden outfit, half size when small.
// Not valid when the content has no stand model.
QcEntityMesher::ModelLook stand(const QcContentRegistry &content, const QcStandPose &pose);
// What a stand wears: armour() and, under a head piece, a filler in the stand's wood
// (QcArmourLook::headFillers) that closes the helmet's face opening.
[[nodiscard]] Dressed standArmour(const QcContentRegistry &content, const QcEquipment &equipment, QcEntityMesher::ModelLook look);
// The bones a stand leaves out: the arms and the base plate when switched off.
quint8 standHiddenBones(const QcStandPose &pose);
// The stand's pose as bone rotations (it does not animate); a cape sways gently as on a figure
// standing still at `seconds`.
QcAnimator::BodyPose standBody(const QcStandPose &pose, double seconds);

// An item as drawn in a hand, on the ground or falling: placeable cube blocks as cubes (or their
// shape), everything else as a sprite of the item texture. An item the content does not know looks
// like nothing (an empty look).
QcEntityMesher::ItemLook item(const QcContentRegistry &content, QcItemId item);
// The item in a figure's hand (world and preview): none for no item or one the content does not
// know.
[[nodiscard]] std::optional<QcEntityMesher::ItemLook> held(const QcContentRegistry &content, QcItemId item);

// Flames around a burning body (decision E-393): four walls with the texture of the content's default
// fire (QcContentRegistry::defaultFire) standing just outside a body `width` wide, inside a cube of
// `height` (BlockPlacement::size), glowing. Nothing when the content has no fire block.
[[nodiscard]] std::optional<QcEntityMesher::ItemLook> flames(const QcContentRegistry &content, double width, double height);

// The state a block item is drawn in (icons, hand, dropped items): fences, walls and panes show a
// piece of a run rather than a lone post.
QcBlockId itemState(const QcContentRegistry &content, QcBlockId block);

} // namespace QcFigureLook
