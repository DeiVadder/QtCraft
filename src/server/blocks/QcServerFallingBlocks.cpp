// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/blocks/QcServerFallingBlocks.h"

#include "common/gameplay/QcAnvil.h"
#include "common/physics/QcPhysics.h"
#include "server/blocks/QcBlockUpdateQueue.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"

#include <algorithm>

QcServerFallingBlocks::QcServerFallingBlocks(QcServerState &state, QcServerSounds &sounds, QcBlockUpdateQueue &updates, QcServerBlocks &blocks,
                                             QcPlayerHealth &health)
    : m_state(state)
    , m_sounds(sounds)
    , m_updates(updates)
    , m_blocks(blocks)
    , m_health(health)
{
}

void QcServerFallingBlocks::start(QcBlockPos pos, QcBlockId block)
{
    if (!m_state.entities().fallingBlocks().isFull()) {
        m_blocks.setAndBroadcast(pos, qcAirBlockId);
        m_state.entities().fallingBlocks().start(pos, block, m_state.entities().newId());
        return;
    }

    // Too many fall already. Without a resting cell (the void below, an unloaded chunk) the block
    // waits for room instead of vanishing, so a collapse of a whole bottom layer goes on entity by
    // entity within the manager's limit.
    const std::optional<QcBlockPos> rest = QcFallingBlockManager::restingCell(m_state.world(), pos);
    if (!rest) {
        m_waiting.insert(pos);
        return;
    }

    m_blocks.setAndBroadcast(pos, qcAirBlockId);

    land({block, *rest, true, static_cast<double>(pos.y - rest->y)});
}

void QcServerFallingBlocks::retryWaiting()
{
    if (m_waiting.isEmpty() || m_state.entities().fallingBlocks().isFull()) {
        return;
    }

    // As a cascade: behind the updates of player actions.
    m_updates.setCommandMode(true);
    for (const QcBlockPos pos : std::as_const(m_waiting)) {
        m_updates.queue(pos);
    }
    m_updates.setCommandMode(false);
    m_waiting.clear();
}

double QcServerFallingBlocks::landingDamage(const QcBlockDefinition &definition, double fallen)
{
    // As in the reference: the blocks fallen beyond the first, rounded up.
    const double steps = std::ceil(fallen - 1.0);
    return steps <= 0.0 ? 0.0 : std::min(std::floor(definition.landingDamage * steps), QcBlockDefinition::landingDamageLimit);
}

void QcServerFallingBlocks::hurtBodies(const QcFallingBlockManager::Landing &landing)
{
    const double damage = landingDamage(m_state.content().block(landing.block), landing.fallen);
    if (damage <= 0.0) {
        return;
    }
    const QcBlockPos cell = landing.pos;
    const QcAabb box{{static_cast<double>(cell.x), static_cast<double>(cell.y), static_cast<double>(cell.z)},
                     {cell.x + 1.0, cell.y + 1.0, cell.z + 1.0}};
    for (const auto &client : m_state.clients()) {
        const std::optional<QcAabb> body = m_state.entities().boxOf(*client);
        if (client->state == QcServerClient::State::Playing && !client->dead && !client->rules().invulnerable && body && body->intersects(box)) {
            m_health.damage(*client, damage, {}, QcPlayerHealth::DamageKind::Armoured);
            m_health.sendHealth(*client);
        }
    }

    // Hurting may remove a creature, so the blows are worked out first.
    QList<QcServerCombat::Blow> blows;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        const std::optional<QcAabb> body = m_state.entities().boxOf(mob);
        if (!mob.dormant && body && body->intersects(box)) {
            blows.append({.creature = mob.id, .damage = damage, .from = QcVec3{cell.x + 0.5, cell.y + 1.0, cell.z + 0.5}, .knockback = 0.0});
        }
    }
    for (const QcServerCombat::Blow &blow : std::as_const(blows)) {
        QcServerCombat::hurtCreature(m_state, m_sounds, blow);
    }
}

QcBlockId QcServerFallingBlocks::wornOnLanding(QcBlockId block, double fallen)
{
    // As in the reference: only a fall that would hurt wears it, with 5 % per block fallen (rounded up).
    const QcBlockDefinition &definition = m_state.content().block(block);
    if (landingDamage(definition, fallen) <= 0.0 || m_state.random().generateDouble() >= QcAnvil::landingWearPerBlock * std::ceil(fallen)) {
        return block;
    }
    const std::optional<QcBlockId> next = definition.wearsInto.isEmpty() ? std::nullopt : m_state.content().blockId(definition.wearsInto);
    return next.value_or(qcAirBlockId);
}

void QcServerFallingBlocks::land(const QcFallingBlockManager::Landing &landing)
{
    if (m_state.content().block(landing.block).landingDamage > 0.0) {
        hurtBodies(landing);
    }
    const QcBlockId block = wornOnLanding(landing.block, landing.fallen);
    const QcBlockPos cell = landing.pos;
    const bool placed = landing.placed;
    const QcBlockDefinition &definition = m_state.content().block(landing.block);
    if (block == qcAirBlockId) {
        // An anvil that broke on landing leaves nothing.
        m_sounds.playBlock(definition, QcBlockSoundAction::Break, cell);
        return;
    }

    // A block landing inside a player or mob would trap it, so it drops as items instead.
    QList<QcAabb> boxes;
    const QcVec3 corner{static_cast<double>(cell.x), static_cast<double>(cell.y), static_cast<double>(cell.z)};
    for (const QcAabb &box : m_state.content().state(block).collision) {
        boxes.append(box.moved(corner));
    }

    if (placed && QcFallingBlockManager::isPassable(m_state.world(), cell) && !m_state.entities().overlapsLiving(boxes)) {
        // Concrete powder landing in water hardens there (decision E-208).
        const std::optional<QcBlockId> replaced = m_state.world().blockAt(cell);
        const bool hardens = replaced && !definition.hardensWith.isEmpty() && m_state.content().isValidBlock(*replaced)
                             && m_state.content().block(*replaced).name == definition.hardensWith;
        const QcBlockId landed = hardens ? m_state.content().blockId(definition.hardensInto).value_or(block) : block;
        m_blocks.setAndBroadcast(cell, landed);
        if (m_state.world().blockAt(cell) == landed) {
            m_sounds.playBlock(definition, QcBlockSoundAction::Place, cell);
            return;
        }
    }

    m_blocks.dropBlockItems(block, cell);
}

void QcServerFallingBlocks::tick()
{
    if (m_state.entities().fallingBlocks().all().isEmpty()) {
        return;
    }

    const QList<QcFallingBlockManager::Landing> landed = m_state.entities().fallingBlocks().tick(m_state.world(), QcPhysics::tickDuration);

    for (const QcFallingBlockManager::Landing &landing : landed) {
        land(landing);
    }
}
