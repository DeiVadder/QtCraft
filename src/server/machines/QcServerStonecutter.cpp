#include "server/machines/QcServerStonecutter.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcStonecutting.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/machines/QcBlockScreens.h"


QcServerStonecutter::QcServerStonecutter(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
    , m_screens(screens)
{
    // One screen at a time: a block's, cart's or villager's screen opening closes the stonecutter.
    m_screens.setStonecutterCloser([this](QcServerClient &client) { close(client, true); });
}

bool QcServerStonecutter::open(QcServerClient &client, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || !m_state.content().isValidBlock(*block) || !m_state.content().block(*block).opensStonecutter) {
        return false;
    }

    // One screen at a time: an open machine, chest, cart or trade closes (decision E-785). The
    // same stonecutter again keeps its slots; another one starts empty.
    m_screens.close(client, true);
    m_screens.endTrade(client);
    m_screens.closeEnchanting(client);
    if (client.screen.stonecutter && client.screen.stonecutter->pos != pos) {
        m_inventory.returnScreenInputs(client);
    }
    if (!client.screen.stonecutter || client.screen.stonecutter->pos != pos) {
        client.screen.stonecutter = QcServerClient::Stonecutter{pos, {}, {}};
    }
    client.connection->send(QcMsg::OpenStonecutter{pos});
    m_inventory.send(client);
    return true;
}

void QcServerStonecutter::close(QcServerClient &client, bool notify)
{
    if (!client.screen.stonecutter) {
        return;
    }
    m_inventory.returnScreenInputs(client);
    client.screen.stonecutter.reset();
    if (client.state == QcServerClient::State::Closed) {
        return;
    }
    if (notify) {
        client.connection->send(QcMsg::CloseStonecutter{});
    }
    m_inventory.send(client);
}

bool QcServerStonecutter::canStayOpen(const QcServerClient &client) const
{
    if (!client.screen.stonecutter || client.dead || !client.rules().interacts) {
        return false;
    }
    const QcBlockPos pos = client.screen.stonecutter->pos;
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    return block && m_state.content().isValidBlock(*block) && m_state.content().block(*block).opensStonecutter
           && QcLineOfSight::isWithinReach(client, pos);
}

void QcServerStonecutter::update()
{
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing && client->screen.stonecutter && !canStayOpen(*client)) {
            close(*client, true);
        }
    }
}

void QcServerStonecutter::select(QcServerClient &client, const QcMsg::SelectStonecutterRecipe &request)
{
    if (!client.screen.stonecutter || !canStayOpen(client)) {
        close(client, true);
        return;
    }

    // An empty name clears the choice; a recipe for another input (or none) is refused, and the
    // client gets the slots as they are.
    QcServerClient::Stonecutter &cutter = *client.screen.stonecutter;
    if (request.recipe.isEmpty() || !QcStonecutting::output(m_state.content(), cutter.input, request.recipe).isEmpty()) {
        cutter.recipe = request.recipe;
    }
    m_inventory.send(client);
}

bool QcServerStonecutter::isOutputSlot(const QcServerClient &client, int slot)
{
    return slot == QcMsg::MoveInventoryItem::machineOutputSlot && client.screen.stonecutter;
}

bool QcServerStonecutter::takeOutput(QcServerClient &client, QcMsg::MoveInventoryItem move)
{
    if (!isOutputSlot(client, move.from)) {
        return false;
    }
    if (!client.screen.stonecutter || !canStayOpen(client)) {
        close(client, true);
        return true;
    }

    // The whole output goes onto an empty slot or a stack of the same item with room for all of it.
    QcServerClient::Stonecutter &cutter = *client.screen.stonecutter;
    const QcContentRegistry &content = m_state.content();
    const QcItemStack output = QcStonecutting::output(content, cutter.input, cutter.recipe);
    if (output.isEmpty() || !QcInventory::isValidSlot(move.to)) {
        m_inventory.send(client);
        return true;
    }
    const QcItemStack target = client.inventory.slot(move.to);
    const int maxStack = content.item(output.item).maxStack;
    const int room = target.isEmpty() ? maxStack : (target.stacksWith(output) ? maxStack - target.count : 0);
    if (output.count > room) {
        m_inventory.send(client);
        return true;
    }

    client.inventory.setSlot(move.to, {output.item, (target.isEmpty() ? 0 : target.count) + output.count, 0});
    cutter.setInput(cutter.input.withCount(cutter.input.count - 1));
    tookOutput(client, 1);
    return true;
}

bool QcServerStonecutter::takeOnce(QcServerClient &client) const
{
    if (!client.screen.stonecutter) {
        return false;
    }
    QcServerClient::Stonecutter &cutter = *client.screen.stonecutter;
    const QcContentRegistry &content = m_state.content();
    const QcItemStack output = QcStonecutting::output(content, cutter.input, cutter.recipe);
    if (output.isEmpty()) {
        return false;
    }

    // All or nothing, on a copy: an output without room for all of it stays where it is.
    QcInventory attempt = client.inventory;
    if (attempt.add(output.item, output.count, content) > 0) {
        return false;
    }
    client.inventory = attempt;
    cutter.setInput(cutter.input.withCount(cutter.input.count - 1));
    return true;
}

bool QcServerStonecutter::takeAllOutput(QcServerClient &client, int slot)
{
    if (!isOutputSlot(client, slot)) {
        return false;
    }
    if (!client.screen.stonecutter || !canStayOpen(client)) {
        close(client, true);
        return true;
    }

    // Each round uses up one input item, so the input's count bounds the rounds.
    int times = 0;
    while (takeOnce(client)) {
        ++times;
    }
    tookOutput(client, times);
    return true;
}

void QcServerStonecutter::tookOutput(QcServerClient &client, int times)
{
    if (times > 0 && client.screen.stonecutter) {
        // canStayOpen checked that a stonecutter stands there; it sounds like itself being struck.
        const QcBlockPos at = client.screen.stonecutter->pos;
        const QcBlockId block = m_state.world().blockAt(at).value_or(qcAirBlockId);
        m_sounds.playBlock(m_state.content().block(block), QcBlockSoundAction::Hit, at);
    }
    m_inventory.send(client);
}
