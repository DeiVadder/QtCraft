// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/machines/QcServerSmithing.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcSmithing.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/machines/QcBlockScreens.h"

QcServerSmithing::QcServerSmithing(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
    , m_screens(screens)
{
    // One screen at a time: a block's, cart's or villager's screen opening closes the table.
    m_screens.setWorkstationCloser(QcWorkstation::Smithing, [this](QcServerClient &client) { close(client, true); });
}

bool QcServerSmithing::isSmithingTable(const QcServerState &state, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = state.world().blockAt(pos);
    return block && state.content().isValidBlock(*block) && state.content().block(*block).opensSmithing;
}

bool QcServerSmithing::open(QcServerClient &client, QcBlockPos pos)
{
    if (!isSmithingTable(m_state, pos)) {
        return false;
    }

    const bool same = client.screen.smithing && client.screen.smithing->pos == pos;
    m_screens.close(client, true);
    m_screens.endTrade(client);
    m_screens.closeWorkstationsExcept(client, QcWorkstation::Smithing);
    if (!same) {
        m_inventory.returnScreenInputs(client);
        client.screen.smithing = QcServerClient::Smithing{pos, {}};
    }
    client.connection->send(QcMsg::OpenSmithing{pos});
    m_inventory.send(client);
    return true;
}

void QcServerSmithing::close(QcServerClient &client, bool notify)
{
    if (!client.screen.smithing) {
        return;
    }
    m_inventory.returnScreenInputs(client);
    client.screen.smithing.reset();
    if (client.state == QcServerClient::State::Closed) {
        return;
    }
    if (notify) {
        client.connection->send(QcMsg::CloseSmithing{});
    }
    m_inventory.send(client);
}

bool QcServerSmithing::canStayOpen(const QcServerClient &client) const
{
    return client.screen.smithing && !client.dead && client.rules().interacts && isSmithingTable(m_state, client.screen.smithing->pos)
           && QcLineOfSight::isWithinReach(client, client.screen.smithing->pos);
}

void QcServerSmithing::update()
{
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing && client->screen.smithing && !canStayOpen(*client)) {
            close(*client, true);
        }
    }
}

QcMsg::SmithingState QcServerSmithing::stateOf(const QcServerState &state, const QcServerClient &client)
{
    QcMsg::SmithingState message;
    if (!client.screen.smithing) {
        return message;
    }
    const QcSmithing::Inputs &inputs = client.screen.smithing->inputs;
    message.templateStack = inputs[static_cast<std::size_t>(QcSmithing::Slot::Template)];
    message.base = inputs[static_cast<std::size_t>(QcSmithing::Slot::Base)];
    message.addition = inputs[static_cast<std::size_t>(QcSmithing::Slot::Addition)];
    message.output = QcSmithing::output(state.content(), inputs);
    return message;
}

void QcServerSmithing::forge(QcServerClient &client, const QcMsg::ForgeSmithing &request)
{
    if (!client.screen.smithing || !canStayOpen(client)) {
        qCDebug(lcServer) << "Smithing: forging by" << client.name << "refused, no smithing table open or in reach";
        close(client, true);
        return;
    }

    // Only the output the client saw counts.
    QcSmithing::Inputs &inputs = client.screen.smithing->inputs;
    const QcItemStack output = QcSmithing::output(m_state.content(), inputs);
    if (output.isEmpty() || !(output == request.output)) {
        qCDebug(lcServer) << "Smithing: forging by" << client.name << "refused: output" << !output.isEmpty() << "same output" << (output == request.output);
        m_inventory.send(client);
        return;
    }

    // The base becomes the output in its slot; one template and one addition are used up.
    const auto useOne = [](QcItemStack &stack) { stack = stack.count > 1 ? stack.withCount(stack.count - 1) : QcItemStack(); };
    useOne(inputs[static_cast<std::size_t>(QcSmithing::Slot::Template)]);
    useOne(inputs[static_cast<std::size_t>(QcSmithing::Slot::Addition)]);
    inputs[static_cast<std::size_t>(QcSmithing::Slot::Base)] = output;

    const QcBlockPos at = client.screen.smithing->pos;
    const QcBlockId block = m_state.world().blockAt(at).value_or(qcAirBlockId);
    m_sounds.playBlock(m_state.content().block(block), QcBlockSoundAction::Hit, at);
    m_inventory.send(client);
}
