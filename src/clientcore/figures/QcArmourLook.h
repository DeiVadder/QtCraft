// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/entitymeshes/QcEntityMesher.h"
#include "common/gameplay/QcEquipment.h"

#include <QList>

class QcContentRegistry;

/*! \brief What worn armour looks like (decision E-235): for each piece its layer boxes, the boxes
    of the reference model (the humanoid) it covers made slightly larger and cut to the layer's
    heights, and its extra parts, as attachments on the bones.

    Players, mannequins and armour stands share it, so a piece looks the same on all of them;
    stands take the humanoid's body size, not their poles. */
namespace QcArmourLook {

/*! Returns the reference model of \a content armour layers are built from. */
const QcModelDefinition *referenceModel(const QcContentRegistry &content);

/*! Returns the attachments of everything in \a equipment that \a content knows; feet first, so
    that a later layer on the same body part (boots over leggings) is drawn over the earlier. */
QList<QcEntityMesher::Attachment> attachments(const QcContentRegistry &content, const QcEquipment &equipment);

/*! Returns the boxes that fill a worn head piece of \a equipment from the inside, as the reference
    model's boxes it covers (not grown), drawn with \a texture all round: a stand has no head behind
    a helmet's face opening, without them the sky would show through it. Empty without a head piece.
    \a content is the registry the armour comes from. */
QList<QcEntityMesher::Attachment> headFillers(const QcContentRegistry &content, const QcEquipment &equipment, const QString &texture);

/*! Returns how far what is worn on the body bone (\a attachments) stands out behind the back of
    \a wearer's body, in metres (at least 0): the gap a cape needs to hang outside a chest layer
    (ModelLook::capeClearance, decisions E-235, E-240). On a stand it also covers the step from
    its pole to the humanoid's back. */
double capeClearance(const QcModelDefinition &wearer, const QList<QcEntityMesher::Attachment> &attachments);

/*! Returns the layer boxes of the piece \a armour on \a reference. */
[[nodiscard]] QList<QcEntityMesher::Attachment> layers(const QcArmourDefinition &armour, const QcModelDefinition &reference);

} // namespace QcArmourLook
