// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/bosses/QcHydraRules.h"

#include "common/content/registry/QcContentRegistry.h"
#include "server/entities/QcCreature.h"
#include "server/mobs/QcMobManager.h"

namespace QcHydraRules {

bool shields(const QcContentRegistry &content, const QcMobManager &creatures, const QcCreature &target,
             const QcProjectileDefinition &projectile, quint32 shooter)
{
    const QcMobDefinition &definition = content.mobs()[target.type];
    if (!definition.hydra) {
        return false;
    }
    if (const QcCreature *by = creatures.find(shooter); by && content.mobs()[by->type].undead) {
        return true;
    }
    return definition.hydra->armouredAt(target.health, definition.maxHealth) && projectile.sticks && projectile.damage > 0.0;
}

std::optional<QcCreatureKindId> sparedBy(const QcContentRegistry &content, const QcMobManager &creatures, quint32 shooter)
{
    const QcCreature *by = creatures.find(shooter);
    if (!by || !content.mobs()[by->type].hydra) {
        return std::nullopt;
    }
    return by->type;
}

bool showsArmour(const QcCreature &hydra, const QcMobDefinition &definition)
{
    if (!hydra.hydra || !definition.hydra) {
        return false;
    }
    const int charge = hydra.hydra->chargeTicks;
    if (charge > 0) {
        return charge > blinkTicks || (charge / 5) % 2 != 1;
    }
    return definition.hydra->armouredAt(hydra.health, definition.maxHealth);
}

} // namespace QcHydraRules
