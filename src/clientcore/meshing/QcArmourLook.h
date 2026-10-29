// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/meshing/QcEntityMesher.h"
#include "common/gameplay/QcEquipment.h"

#include <QList>

class QcContentRegistry;

// What worn armour looks like (decision E-235): for each piece its layer boxes, the boxes of the
// reference model (the humanoid) it covers made slightly larger and cut to the layer's heights,
// and its extra parts, as attachments on the bones. Players, mannequins and armour stands share it,
// so a piece looks the same on all of them; stands take the humanoid's body size, not their poles.
namespace QcArmourLook {

// The reference model armour layers are built from.
const QcModelDefinition *referenceModel(const QcContentRegistry &content);

// Attachments of everything worn that the content knows; feet first, so that a later layer on the
// same body part (boots over leggings) is drawn over the earlier.
QList<QcEntityMesher::Attachment> attachments(const QcContentRegistry &content, const QcEquipment &equipment);

// Boxes that fill a worn head piece from the inside, as the reference model's boxes it covers
// (not grown), drawn with `texture` all round: a stand has no head behind a helmet's face opening,
// without them the sky would show through it. Empty without a head piece.
QList<QcEntityMesher::Attachment> headFillers(const QcContentRegistry &content, const QcEquipment &equipment, const QString &texture);

// How far what is worn on the body bone stands out behind the back of `wearer`'s body, in metres
// (at least 0): the gap a cape needs to hang outside a chest layer (ModelLook::capeClearance,
// decisions E-235, E-240). On a stand it also covers the step from its pole to the humanoid's back.
double capeClearance(const QcModelDefinition &wearer, const QList<QcEntityMesher::Attachment> &attachments);

// The layer boxes of one piece on `reference`.
[[nodiscard]] QList<QcEntityMesher::Attachment> layers(const QcArmourDefinition &armour, const QcModelDefinition &reference);

} // namespace QcArmourLook
