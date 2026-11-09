// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcCreatureKindId.h"

#include <QtGlobal>

#include <optional>

class QcContentRegistry;
struct QcMobDefinition;
class QcMobManager;
struct QcCreature;
struct QcProjectileDefinition;

/*! \brief What shields the charred hydra (decision E-1126; the role model's wither) from the shots
    and blasts of the projectiles of QcServerProjectiles. */
namespace QcHydraRules {

/*! Returns whether a hit of a projectile of the kind \a projectile shot by \a shooter (an entity
    id, 0 for none) leaves \a target, a creature of \a content among \a creatures, unhurt: a hydra
    is never hurt by the shots of undead creatures (its own skulls among them), and while it wears
    its armour projectiles that stick in blocks and hurt bounce off (arrows: the role model's
    arrows and tridents). Always false for any other creature. */
[[nodiscard]] bool shields(const QcContentRegistry &content, const QcMobManager &creatures, const QcCreature &target,
                           const QcProjectileDefinition &projectile, quint32 shooter);

/*! Returns the kind of creature of \a content among \a creatures that the blast of a projectile
    shot by \a shooter spares: a hydra's kind for the skulls of a hydra; none otherwise. */
[[nodiscard]] std::optional<QcCreatureKindId> sparedBy(const QcContentRegistry &content, const QcMobManager &creatures, quint32 shooter);

/*! Ticks before the end of a charge from which the armour of a charging hydra blinks (the role
    model's 80); before that it glows throughout. */
inline constexpr int blinkTicks = 80;

/*! Returns whether \a hydra, a creature of \a definition, shows its armour (QcMsg::Armoured): in
    its armour (QcHydraDefinition::armouredAt), and while it charges, blinking every five ticks
    for the last blinkTicks. */
[[nodiscard]] bool showsArmour(const QcCreature &hydra, const QcMobDefinition &definition);

} // namespace QcHydraRules
