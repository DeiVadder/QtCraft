// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/entities/QcCreature.h"

#include "common/content/mobs/QcMobDefinition.h"

void QcCreature::addBrainState(const QcMobDefinition &definition)
{
    using Ai = QcMobDefinition::Ai;
    if (definition.ai == Ai::Archer) {
        archer.emplace();
    } else if (definition.ai == Ai::Drifter) {
        drifter.emplace();
    } else if (definition.ai == Ai::Hydra) {
        hydra.emplace();
    } else if (definition.ai == Ai::Golem) {
        golem.emplace();
    }
}
