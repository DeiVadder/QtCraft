// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/machines/QcServerAnvil.h"

#include "common/content/registry/QcContentRegistry.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/machines/QcBlockScreens.h"

QcServerAnvil::QcServerAnvil(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens, QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
    , m_screens(screens)
    , m_blocks(blocks)
{
    // One screen at a time: a block's, cart's or villager's screen opening closes the anvil.
    m_screens.setAnvilCloser([this](QcServerClient &client) { close(client, true); });
}

bool QcServerAnvil::isAnvil(const QcServerState &state, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = state.world().blockAt(pos);
    return block && state.content().isValidBlock(*block) && state.content().block(*block).opensAnvil;
}

bool QcServerAnvil::open(QcServerClient &client, QcBlockPos pos)
{
    if (!isAnvil(m_state, pos)) {
        return false;
    }

    const bool same = client.screen.anvil && client.screen.anvil->pos == pos;
    m_screens.close(client, true);
    m_screens.endTrade(client);
    m_screens.closeStonecutter(client);
    m_screens.closeEnchanting(client);
    if (!same) {
        m_inventory.returnScreenInputs(client);
        client.screen.anvil = QcServerClient::Anvil{pos, {}, {}, {}};
    }
    client.connection->send(QcMsg::OpenAnvil{pos});
    m_inventory.send(client);
    return true;
}

void QcServerAnvil::close(QcServerClient &client, bool notify)
{
    if (!client.screen.anvil) {
        return;
    }
    m_inventory.returnScreenInputs(client);
    client.screen.anvil.reset();
    if (client.state == QcServerClient::State::Closed) {
        return;
    }
    if (notify) {
        client.connection->send(QcMsg::CloseAnvil{});
    }
    m_inventory.send(client);
}

bool QcServerAnvil::canStayOpen(const QcServerClient &client) const
{
    return client.screen.anvil && !client.dead && client.rules().interacts && isAnvil(m_state, client.screen.anvil->pos)
           && QcLineOfSight::isWithinReach(client, client.screen.anvil->pos);
}

void QcServerAnvil::update()
{
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing && client->screen.anvil && !canStayOpen(*client)) {
            close(*client, true);
        }
    }
}

QcAnvil::Result QcServerAnvil::resultFor(const QcServerState &state, const QcServerClient &client)
{
    if (!client.screen.anvil) {
        return {};
    }
    const QcServerClient::Anvil &anvil = *client.screen.anvil;
    return QcAnvil::result(state.content(), anvil.left, anvil.right, anvil.name);
}

QcMsg::AnvilState QcServerAnvil::stateOf(const QcServerState &state, const QcServerClient &client)
{
    QcMsg::AnvilState message;
    if (!client.screen.anvil) {
        return message;
    }
    const QcAnvil::Result result = resultFor(state, client);
    message.left = client.screen.anvil->left;
    message.right = client.screen.anvil->right;
    message.name = client.screen.anvil->name;
    message.output = result.output;
    message.levelCost = result.levelCost;
    return message;
}

void QcServerAnvil::rename(QcServerClient &client, const QcMsg::RenameAnvil &request)
{
    if (!client.screen.anvil || !canStayOpen(client)) {
        qCDebug(lcServer) << "Anvil: rename by" << client.name << "refused, no anvil open or in reach";
        close(client, true);
        return;
    }
    // Only the anvil's state changes; the inventory stays as it is.
    client.screen.anvil->name = QcAnvil::nameFrom(request.name);
    client.connection->send(stateOf(m_state, client));
}

void QcServerAnvil::forge(QcServerClient &client, const QcMsg::ForgeAnvil &request)
{
    if (!client.screen.anvil || !canStayOpen(client)) {
        qCDebug(lcServer) << "Anvil: forging by" << client.name << "refused, no anvil open or in reach";
        close(client, true);
        return;
    }

    // Only the output and cost the client saw count; creative players pay nothing and are never
    // refused (the reference's rule), others need the levels and a cost below QcAnvil::tooExpensive.
    const QcAnvil::Result result = resultFor(m_state, client);
    const bool free = client.rules().itemCatalogue;
    const bool affordable = free || (!result.isTooExpensive() && client.experience.level() >= result.levelCost);
    if (result.isEmpty() || result.levelCost != request.levelCost || !(result.output == request.output) || !affordable) {
        qCDebug(lcServer) << "Anvil: forging by" << client.name << "refused: output" << !result.isEmpty() << "cost" << result.levelCost
                          << "asked" << request.levelCost << "same output" << (result.output == request.output) << "affordable" << affordable;
        m_inventory.send(client);
        return;
    }

    if (!free) {
        [[maybe_unused]] const bool paid = client.experience.spendLevels(result.levelCost);
    }
    QcServerClient::Anvil &anvil = *client.screen.anvil;
    anvil.setLeft(result.output);
    anvil.name = result.output.name();
    anvil.right = anvil.right.count > result.rightUsed ? anvil.right.withCount(anvil.right.count - result.rightUsed) : QcItemStack();

    const QcBlockPos at = anvil.pos;
    const QcBlockId block = m_state.world().blockAt(at).value_or(qcAirBlockId);
    m_sounds.playBlock(m_state.content().block(block), QcBlockSoundAction::Hit, at);
    if (!free && m_state.random().generateDouble() < QcAnvil::wearChance) {
        wear(m_state, m_blocks, m_sounds, at);
    }
    m_inventory.send(client);
}

void QcServerAnvil::wear(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = state.world().blockAt(pos);
    if (!block || !state.content().isValidBlock(*block)) {
        return;
    }
    const QcBlockDefinition &definition = state.content().block(*block);
    const std::optional<QcBlockId> next = definition.wearsInto.isEmpty() ? std::nullopt : state.content().blockId(definition.wearsInto);
    sounds.playBlock(definition, QcBlockSoundAction::Break, pos);
    blocks.setAndBroadcast(pos, next.value_or(qcAirBlockId));
}
