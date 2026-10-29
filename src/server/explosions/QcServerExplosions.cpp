// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/explosions/QcServerExplosions.h"

#include "common/content/registry/QcSoundKeys.h"
#include "common/physics/QcPhysics.h"
#include "server/blocks/QcFireRules.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/explosions/QcBlastField.h"
#include "server/explosions/QcExplosionRays.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

// What the dice of lighting decide.
constexpr quint32 HopSalt = 130;
constexpr quint32 ChainSalt = 140;

constexpr quint32 hopSteps = 360;

} // namespace

QcServerExplosions::QcServerExplosions(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, QcServerFire &fire,
                                       QcPlayerHealth &health)
    : m_state(state)
    , m_sounds(sounds)
    , m_table(state.content())
    , m_blocks(state, blocks, fire, m_table)
    , m_bodies(state, sounds, health)
{
}

bool QcServerExplosions::breaksBlocks() const
{
    return m_state.info().explosionBlockDamage;
}

bool QcServerExplosions::light(QcBlockPos pos, std::optional<int> fuse)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || !isExplosive(*block)) {
        return false;
    }
    const bool lit = lightBlock(pos, *block, fuse.value_or(m_table.blast(*block).fuseTicks));
    m_blocks.flush();
    return lit;
}

bool QcServerExplosions::lightBlock(QcBlockPos pos, QcBlockId block, int fuse)
{
    // Two blasts may reach the same explosive in one batch: only the first lights it.
    if (m_state.world().blockAt(pos) != block) {
        return false;
    }
    QcPrimedExplosives &explosives = m_state.entities().explosives();
    if (explosives.isFull()) {
        ++m_numbers.refused;
        return false;
    }
    if (!m_blocks.clear(pos)) {
        return false;
    }

    // The hop to the side by dice, so the same world lights the same.
    const QcFireRules::Dice dice{static_cast<quint64>(m_state.config().seed), pos, 0};
    const double sideways = 2.0 * std::numbers::pi * dice.roll(pos, HopSalt, hopSteps) / hopSteps;
    explosives.prime(pos, block, std::max(1, fuse), m_state.entities().newId(), sideways);
    ++m_numbers.lit;
    m_sounds.play(QString::fromLatin1(QcSoundKeys::explosivePrimed), {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
    return true;
}

int QcServerExplosions::chainFuse(QcBlockPos pos, QcBlockId block) const
{
    const QcBlastDefinition &blast = m_table.blast(block);
    const QcFireRules::Dice dice{static_cast<quint64>(m_state.config().seed), pos, 1};
    const auto spread = static_cast<quint32>(blast.chainFuseMax - blast.chainFuseMin + 1);
    return blast.chainFuseMin + static_cast<int>(dice.roll(pos, ChainSalt, spread));
}

void QcServerExplosions::explode(const Blast &blast)
{
    ++m_numbers.explosions;
    const QcBlockPos centre = QcPhysics::blockContaining(blast.centre);
    const int radius = std::max(QcExplosionRays::reach(blast.power), static_cast<int>(std::ceil(QcExplosionRays::reachFactor * blast.power)) + 1);
    QcBlastField field(m_state.world(), m_table, centre, radius);

    if (blast.breaksBlocks) {
        const QcExplosionRays::Blast rays{blast.centre, blast.power, static_cast<quint64>(m_state.config().seed)};
        const qsizetype limit = static_cast<qsizetype>(m_state.config().limits.explosionBlocks);
        m_blocks.add(QcExplosionRays::broken(field, rays, limit), {centre, 1.0 / blast.power, blast.fire});
    }

    const QcBlastBodies::Hits hits = m_bodies.hurt(field, blast.centre, blast.power);
    m_numbers.playersHit += static_cast<quint64>(hits.players);
    m_numbers.creaturesHit += static_cast<quint64>(hits.creatures);
    announce(blast);
}

void QcServerExplosions::announce(const Blast &blast)
{
    // Louder and further for stronger blasts (decision E-879): TNT (power 4) sounds at 1.1, so that its
    // rendering (peak 0.9) peaks at about 1.0 and one blast stays near the client's limiter knee; its
    // loudness comes from the rendering's drive. Stronger blasts go up to 1.4.
    const float volume = std::clamp(0.275f * static_cast<float>(blast.power), 0.8f, 1.4f);
    m_sounds.play(QString::fromLatin1(QcSoundKeys::explosion), blast.centre, volume);
    const QcMsg::Explosion message{blast.centre, static_cast<float>(blast.power)};
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing && (client->motion.position - blast.centre).length() <= viewDistance) {
            client->connection->send(message);
        }
    }
}

void QcServerExplosions::tick()
{
    const QcServerLimits &limits = m_state.config().limits;
    QcPrimedExplosives &explosives = m_state.entities().explosives();
    if (explosives.all().isEmpty() && m_blocks.waiting() == 0) {
        return;
    }

    const QList<QcPrimedExplosive> due = explosives.tick(m_state.world(), QcPhysics::tickDuration, static_cast<qsizetype>(limits.explosionsPerTick));
    m_numbers.waited += static_cast<quint64>(std::ranges::count_if(explosives.all(),
        [](const QcPrimedExplosive &primed) { return primed.fuse == 0 && !primed.dormant; }));
    for (const QcPrimedExplosive &primed : due) {
        const QcBlastDefinition &definition = m_table.blast(primed.block);
        const QcVec3 centre = primed.motion.position + QcVec3{0.0, QcPrimedExplosives::size * 0.5, 0.0};
        explode({centre, definition.power, breaksBlocks(), definition.fire});
    }

    // The game rule may have changed while cells waited: then they stay as they are.
    if (!breaksBlocks()) {
        m_blocks.drop();
    }
    const QList<QcBlastBlocks::Chained> chain = m_blocks.take(limits.explosionBlocksPerTick);
    for (const QcBlastBlocks::Chained &chained : chain) {
        if (lightBlock(chained.pos, chained.block, chainFuse(chained.pos, chained.block))) {
            ++m_numbers.chained;
        }
    }
    m_blocks.flush();
}
