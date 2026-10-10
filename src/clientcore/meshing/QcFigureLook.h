#pragma once

#include "QcAppearance.h"
#include "QcEntityMesher.h"

#include <optional>

class QcContentRegistry;

// What players and items look like when drawn: shared by the world (QcRenderBridge) and the
// character preview, so the preview shows exactly what other players see.
namespace QcFigureLook {

// The humanoid model with the appearance's outfit and hair; an outfit the content does not offer
// falls back to the default outfit, a hair style of the wrong kind to none. Not valid when the
// content has no humanoid model.
QcEntityMesher::ModelLook player(const QcContentRegistry &content, const QcAppearance &appearance);

// An item as drawn in a hand, on the ground or falling: placeable cube blocks as cubes (or their
// shape), everything else as a sprite of the item texture. An item the content does not know looks
// like nothing (an empty look).
QcEntityMesher::ItemLook item(const QcContentRegistry &content, QcItemId item);
// The item in a figure's hand (world and preview): none for no item or one the content does not
// know.
std::optional<QcEntityMesher::ItemLook> held(const QcContentRegistry &content, QcItemId item);

// The state a block item is drawn in (icons, hand, dropped items): fences, walls and panes show a
// piece of a run rather than a lone post.
QcBlockId itemState(const QcContentRegistry &content, QcBlockId block);

} // namespace QcFigureLook
