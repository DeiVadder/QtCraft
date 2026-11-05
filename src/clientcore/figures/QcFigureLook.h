// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/entitymeshes/QcEntityMesher.h"
#include "common/content/mobs/QcAppearance.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcStandPose.h"

#include <functional>
#include <memory>
#include <optional>

class QcContentRegistry;
class QcSpriteShapes;
class QcTextureAtlas;
struct QcLauncherDefinition;

/*! \brief What players and items look like when drawn: shared by the world
    (QcEntityFrameBuilder) and the character preview, so the preview shows exactly what other
    players see. */
namespace QcFigureLook {

/*! Returns the humanoid model of \a content with the outfit, hair and cape of \a appearance; an
    outfit the content does not offer falls back to the default outfit, a hair style or cape of
    the wrong kind to none. Not valid when the content has no humanoid model. */
QcEntityMesher::ModelLook player(const QcContentRegistry &content, const QcAppearance &appearance);
/*! Returns the cape of \a content called \a name; none for an empty name, an unknown one or
    another kind of appearance. */
const QcAppearanceDefinition *cape(const QcContentRegistry &content, const QString &name);

/*! \brief A figure's look with what it wears. */
struct Dressed
{
    /*! The look, with the hair left out under a helmet and the cape's clearance set. */
    QcEntityMesher::ModelLook look;
    /*! The attachments of the armour worn. */
    QList<QcEntityMesher::Attachment> armour;
};

/*! Returns what a figure wears: the attachments of the armour in \a equipment (QcArmourLook) and \a
    look with it: under a helmet the hair is left out, so it does not poke through, and a cape hangs
    outside a chest layer (ModelLook::capeClearance). \a content is the registry the armour comes
    from. */
[[nodiscard]] Dressed armour(const QcContentRegistry &content, const QcEquipment &equipment, QcEntityMesher::ModelLook look);

/*! Returns the armour stand of \a content in \a pose (decision E-238): the stand model with its
    wooden outfit, half size when small. Not valid when the content has no stand model. */
QcEntityMesher::ModelLook stand(const QcContentRegistry &content, const QcStandPose &pose);
/*! Returns what a stand wears of \a equipment, with its \a look, from \a content: armour() and,
    under a head piece, a filler in the stand's wood (QcArmourLook::headFillers) that closes the
    helmet's face opening. */
[[nodiscard]] Dressed standArmour(const QcContentRegistry &content, const QcEquipment &equipment, QcEntityMesher::ModelLook look);
/*! Returns the bones a stand in \a pose leaves out: the arms and the base plate when switched
    off. */
quint8 standHiddenBones(const QcStandPose &pose);
/*! Returns the stand's \a pose as bone rotations (it does not animate); a cape sways gently as on a
    figure standing still at \a seconds. */
QcAnimator::BodyPose standBody(const QcStandPose &pose, double seconds);

/*! Returns the look of \a item as drawn in a hand, on the ground or falling: placeable cube blocks
    as cubes (or their shape), everything else as a sprite of the item texture. An item \a content
    does not know looks like nothing (an empty look). How a hand holds it comes from the item's \c
    {hold}, then its block's, then its category's defaults (decision E-831). */
QcEntityMesher::ItemLook item(const QcContentRegistry &content, QcItemId item);
/*! Returns the look of \a item of \a content in a figure's hand (world and preview): none for no
    item or one the content does not know. */
[[nodiscard]] std::optional<QcEntityMesher::ItemLook> held(const QcContentRegistry &content, QcItemId item);
/*! Returns the launcher of \a item of \a content: null for no item, one the content does not know
    or one that is no launcher. */
[[nodiscard]] const QcLauncherDefinition *launcher(const QcContentRegistry &content, QcItemId item);
/*! Returns the draw stage (decision E-987) of a launcher drawn with the power \a drawProgress
    (QcEntityPose::drawProgress): 0 when it is not drawn, 1 from the start of the draw, 2 from 65 %
    and 3 from 90 % of the draw time, the reference's borders. */
[[nodiscard]] int drawStage(double drawProgress);
/*! Returns the texture name of the draw stage \a stage (1 to 3) of the sprite \a sprite. */
[[nodiscard]] QString drawSprite(const QString &sprite, int stage);
/*! Returns \a look with the sprite of the draw stage of \a drawProgress. A stage \a hasTexture
    does not know keeps the resting sprite, so a launcher without stage textures looks as it
    always did. */
[[nodiscard]] QcEntityMesher::ItemLook atDraw(QcEntityMesher::ItemLook look, double drawProgress,
                                              const std::function<bool(const QString &)> &hasTexture);
/*! Returns the look of \a item of \a content in a figure's hand at \a drawProgress (negative when
    not drawn): the sprite of its stage with that sprite's shape from \a shapes, the stages that
    \a atlas holds, and the grip of the resting sprite. None as held(). */
[[nodiscard]] std::optional<QcEntityMesher::ItemLook> heldAtDraw(const QcContentRegistry &content, QcItemId item, double drawProgress,
                                                                 QcSpriteShapes &shapes,
                                                                 const std::shared_ptr<const QcTextureAtlas> &atlas);

/*! Returns the look of the dropped \a item of \a content: as item(), a sprite with the shape
    \a atlas gives it through \a shapes, so that it lies as one solid item (decision E-991); flat
    without an atlas. */
[[nodiscard]] QcEntityMesher::ItemLook dropped(const QcContentRegistry &content, QcItemId item, QcSpriteShapes &shapes,
                                               const std::shared_ptr<const QcTextureAtlas> &atlas);

/*! Returns the flames around a burning body (decision E-393): four walls with the texture of the
    default fire of \a content (QcContentRegistry::defaultFire) standing just outside a body \a
    width wide, inside a cube of \a height (BlockPlacement::size), glowing; nothing when the content
    has no fire block. */
[[nodiscard]] std::optional<QcEntityMesher::ItemLook> flames(const QcContentRegistry &content, double width, double height);

/*! Returns the state of \a block of \a content a block item is drawn in (icons, hand, dropped
    items): fences, walls and panes show a piece of a run rather than a lone post. */
QcBlockId itemState(const QcContentRegistry &content, QcBlockId block);

} // namespace QcFigureLook
