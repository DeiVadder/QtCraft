// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/dimensions/QcServerAnchors.h"

#include "common/blockrules/QcBlockRules.h"
#include "common/blockrules/QcSafeStand.h"
#include "common/protocol/QcChatText.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/explosions/QcServerExplosions.h"
#include "server/gameplay/QcServerInventory.h"

#include <array>

using namespace Qt::StringLiterals;

namespace {

// Where a respawn beside an anchor looks, in this order: the eight cells around it at its height,
// the same one below and one above, then on top of it (the reference's order, roughly).
constexpr std::array<std::array<int, 2>, 8> around{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}}};

QcVec3 middleOf(QcBlockPos pos)
{
    return {pos.x + 0.5, static_cast<double>(pos.y), pos.z + 0.5};
}

} // namespace

QcServerAnchors::QcServerAnchors(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory,
                                 QcServerExplosions &explosions)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
    , m_inventory(inventory)
    , m_explosions(explosions)
{
}

bool QcServerAnchors::use(QcServerClient &client, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || !m_state.content().isValidBlock(*block)) {
        return false;
    }
    const QcBlockDefinition &definition = m_state.content().block(*block);
    if (definition.anchor.isAnchor()) {
        return useAnchor(client, pos, definition);
    }
    return definition.bed && useBed(pos, *block);
}

bool QcServerAnchors::useAnchor(QcServerClient &client, QcBlockPos pos, const QcBlockDefinition &anchor)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    const bool fuel = held && held->name == anchor.anchor.fuel;
    if (fuel && !anchor.anchor.charged.isEmpty()) {
        charge(client, pos, anchor);
        return true;
    }
    if (anchor.anchor.charge == 0) {
        // An empty anchor takes nothing but its fuel; other items act on it as on any block.
        return false;
    }
    if (m_state.current().definition().anchorsWork) {
        bind(client, pos, anchor);
    } else {
        blowUp(pos, std::nullopt);
    }
    return true;
}

void QcServerAnchors::charge(QcServerClient &client, QcBlockPos pos, const QcBlockDefinition &anchor)
{
    const std::optional<QcBlockId> charged = m_state.content().blockId(anchor.anchor.charged);
    if (!charged) {
        return;
    }
    m_blocks.setAndBroadcast(pos, *charged);
    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_inventory.send(client);
    const QcBlockDefinition &now = m_state.content().block(*charged);
    m_sounds.playBlock(now, QcBlockSoundAction::Place, pos);
    // The charged state counts as placed: it has no item, so nothing else places it.
    m_state.advancements().trigger(client, {.trigger = QcAdvancementTrigger::BlockPlaced, .block = now.name});
}

void QcServerAnchors::bind(QcServerClient &client, QcBlockPos pos, const QcBlockDefinition &anchor)
{
    const QcRespawnAnchor bound{m_state.current().name(), pos};
    // As in the reference, the message comes only when the point changes.
    if (client.anchor != bound) {
        client.anchor = bound;
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Respawn point set")));
    }
    const QcItemDefinition *held = m_state.heldItem(client);
    m_state.advancements().trigger(client, {.trigger = QcAdvancementTrigger::ItemUsedOnBlock, .item = held ? held->name : QString(), .block = anchor.name});
}

bool QcServerAnchors::useBed(QcBlockPos pos, QcBlockId block)
{
    if (!m_state.current().definition().bedsExplode) {
        return false;
    }
    blowUp(pos, QcBlockRules::otherHalf(m_state.world(), pos, block));
    return true;
}

void QcServerAnchors::blowUp(QcBlockPos pos, std::optional<QcBlockPos> other)
{
    // The blocks go without drops before the blast, which then breaks what is around.
    m_blocks.setAndBroadcast(pos, qcAirBlockId);
    if (other) {
        m_blocks.setAndBroadcast(*other, qcAirBlockId);
    }
    const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
    m_explosions.explode({.centre = centre, .power = explosionPower, .breaksBlocks = m_explosions.breaksBlocks(), .fire = true});
}

std::optional<QcServerAnchors::Respawn> QcServerAnchors::respawnAt(QcServerClient &client)
{
    if (!client.anchor) {
        return std::nullopt;
    }
    // A dimension that cannot open now (a mod missing) keeps the anchor for later, like the
    // return place of decision E-989.
    const QcResult<QcServerDimension *> opened = m_state.openDimension(client.anchor->dimension);
    if (!opened) {
        return std::nullopt;
    }

    const QcServerState::Scope scope(m_state, **opened);
    const QcBlockPos pos = client.anchor->pos;
    m_state.world().loadChunk(pos.chunk());
    const std::optional<QcBlockId> anchor = chargedAnchor(pos);
    const std::optional<QcVec3> feet = anchor ? besideAnchor(pos) : std::nullopt;
    if (!feet) {
        forget(client);
        return std::nullopt;
    }

    // The respawn takes one charge.
    if (const std::optional<QcBlockId> spent = m_state.content().blockId(m_state.content().block(*anchor).anchor.spent)) {
        m_blocks.setAndBroadcast(pos, *spent);
    }
    return Respawn{*opened, *feet};
}

std::optional<QcBlockId> QcServerAnchors::chargedAnchor(QcBlockPos pos) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || !m_state.content().isValidBlock(*block)) {
        return std::nullopt;
    }
    const QcAnchorDefinition &anchor = m_state.content().block(*block).anchor;
    return anchor.isAnchor() && anchor.charge > 0 ? block : std::nullopt;
}

std::optional<QcVec3> QcServerAnchors::besideAnchor(QcBlockPos pos) const
{
    const QcServerWorld &world = m_state.world();
    for (const int dy : {0, -1, 1}) {
        for (const auto &[dx, dz] : around) {
            // The chunks beside the anchor's may not be loaded yet.
            const QcBlockPos feet = pos.offset(dx, dy, dz);
            m_state.world().loadChunk(feet.chunk());
            if (QcSafeStand::roomAt(world, feet)) {
                return middleOf(feet);
            }
        }
    }
    const QcBlockPos top = pos.offset(0, 1, 0);
    return QcSafeStand::roomAt(world, top) ? std::optional<QcVec3>(middleOf(top)) : std::nullopt;
}

void QcServerAnchors::forget(QcServerClient &client)
{
    client.anchor.reset();
    client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "You have no charged soul anchor, or it was blocked")));
}
