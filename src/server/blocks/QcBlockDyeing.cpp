// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/blocks/QcBlockDyeing.h"

#include "server/blockentities/QcServerSigns.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerInventory.h"

QcBlockDyeing::QcBlockDyeing(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_blocks(blocks)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

bool QcBlockDyeing::dye(QcServerClient &client, QcBlockPos target, const QcItemDefinition *held)
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> clicked = m_state.world().blockAt(target);
    if (!held || held->dye.isEmpty() || !clicked || !content.isValidBlock(*clicked) || client.dead) {
        return false;
    }

    const QcBlockDefinition &block = content.block(*clicked);
    const QcBlockProperty *property = block.dyeProperty.isEmpty() ? nullptr : block.property(block.dyeProperty);
    if (!property || !property->values.contains(held->dye) || !QcLineOfSight::isWithinReach(client, target)) {
        return false;
    }
    if (content.property(*clicked, block.dyeProperty) == held->dye) {
        return true;
    }

    m_blocks.setAndBroadcast(target, content.withProperty(*clicked, block.dyeProperty, held->dye));
    useDye(client, target, block);
    return true;
}

bool QcBlockDyeing::dyeSign(QcServerClient &client, QcBlockPos target, const QcItemDefinition *held)
{
    const std::optional<QString> color = held ? QcSignText::colorOfDye(held->dye) : std::nullopt;
    QcSignTexts &signs = m_state.world().signs();
    const QcSignTexts::Lines *lines = signs.at(target);
    // As in the model: a dye works on written signs only; on a blank one the editor opens.
    if (!color || !lines || QcSignText::isEmpty(*lines) || !QcServerSigns(m_state).mayWrite(client, target)) {
        return false;
    }
    if (signs.colorAt(target) == *color) {
        return true;
    }

    signs.setColor(target, *color);
    useDye(client, target, m_state.content().block(m_state.world().blockAt(target).value_or(qcAirBlockId)));
    return true;
}

void QcBlockDyeing::useDye(QcServerClient &client, QcBlockPos target, const QcBlockDefinition &block)
{
    m_sounds.playBlock(block, QcBlockSoundAction::Place, target);
    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_inventory.send(client);
}
