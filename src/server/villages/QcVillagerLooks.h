// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcAppearance.h"
#include "common/core/QcCreatureKindId.h"

class QcContentRegistry;
struct QcVillagerRecord;

/*! \brief What a villager looks like (decision E-411): the look of its village style (the default
    look of its species, villagers/<name>.json, for a style without one), its profession's outfit
    (the style's outfit with the profession's layers, QcVillagerOutfits) and hat in place of the
    style's hair, and one of each of the style's hair styles and colours drawn from the villager's
    seed.

    The server sends it with the villager (QcMsg::EntitySpawn::appearance). */
namespace QcVillagerLooks {

/*! Returns the look of \a villager, a creature of kind \a mobType of \a content. */
QcAppearance appearanceOf(const QcContentRegistry &content, QcCreatureKindId mobType, const QcVillagerRecord &villager);

} // namespace QcVillagerLooks
