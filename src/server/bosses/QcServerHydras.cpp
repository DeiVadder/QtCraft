// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/bosses/QcServerHydras.h"

#include "server/blocks/QcServerBlocks.h"
#include "server/bosses/QcHydraBrain.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/explosions/QcServerExplosions.h"

#include <cmath>

// A hydra's request of this tick, taken before any of them is served: a blast may remove
// creatures from the list it was found in.
struct QcServerHydras::Request
{
    QcCreatureKindId type;
    QcVec3 feet;
    QcVec3 head;
    bool blast = false;
    bool breaks = false;
};

QcServerHydras::QcServerHydras(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, QcServerExplosions &explosions)
    : m_state(state)
    , m_blocks(blocks)
    , m_sounds(sounds)
    , m_explosions(explosions)
{
}

void QcServerHydras::tick()
{
    QList<Request> requests;
    for (QcCreature &mob : m_state.entities().creatures().editableMobs()) {
        if (!mob.hydra || (!mob.hydra->blastDue && !mob.hydra->breakDue)) {
            continue;
        }
        requests.append({mob.type, mob.motion.position, QcHydraBrain::headOf(mob, 0), std::exchange(mob.hydra->blastDue, false),
                         std::exchange(mob.hydra->breakDue, false)});
    }
    for (const Request &request : requests) {
        if (request.blast) {
            blast(request);
        }
        if (request.breaks) {
            breakAround(request);
        }
    }
}

void QcServerHydras::blast(const Request &request)
{
    const QcHydraDefinition &hydra = *m_state.content().mobs()[request.type].hydra; // NOLINT(bugprone-unchecked-optional-access): only creatures of Ai::Hydra carry its state, their kinds its data
    m_sounds.play(hydra.summonSound, request.head);
    if (hydra.blastPower <= 0.0) {
        return;
    }
    m_explosions.explode({.centre = request.head,
                          .power = hydra.blastPower,
                          .breaksBlocks = m_explosions.breaksBlocks() && m_state.info().mobGriefing,
                          .fire = false,
                          .resistanceLimit = -1.0,
                          .spares = request.type});
}

void QcServerHydras::breakAround(const Request &request)
{
    const QcBlockPos feet{static_cast<int>(std::floor(request.feet.x)), static_cast<int>(std::floor(request.feet.y)),
                          static_cast<int>(std::floor(request.feet.z))};
    bool broke = false;
    // Every cell of the column, layer by layer from the feet up.
    constexpr int layer = breakColumn * breakColumn;
    for (int cell = 0; cell < layer * breakHeight; ++cell) {
        const QcBlockPos pos = feet.offset(cell % breakColumn - breakColumn / 2, cell / layer, cell % layer / breakColumn - breakColumn / 2);
        if (breakable(pos)) {
            m_blocks.breakBlock(pos, true);
            broke = true;
        }
    }
    if (broke) {
        const QcMobDefinition &definition = m_state.content().mobs()[request.type];
        m_sounds.play(definition.hydra->breakSound, request.head); // NOLINT(bugprone-unchecked-optional-access): only creatures of Ai::Hydra carry its state, their kinds its data
    }
}

bool QcServerHydras::breakable(QcBlockPos pos) const
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || *block == qcAirBlockId || !content.isValidBlock(*block)) {
        return false;
    }
    const QcBlockDefinition &definition = content.block(*block);
    // Unbreakable blocks stay, except a portal (the role model's wither breaks it).
    return !definition.liquid && (definition.hardness >= 0.0 || definition.support == QcBlockSupport::Portal);
}
