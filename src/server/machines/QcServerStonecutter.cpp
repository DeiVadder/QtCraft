#include "server/machines/QcServerStonecutter.h"

#include "common/content/registry/QcContentRegistry.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerInventory.h"


QcServerStonecutter::QcServerStonecutter(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

bool QcServerStonecutter::open(QcServerClient &client, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || !m_state.content().isValidBlock(*block) || !m_state.content().block(*block).opensStonecutter) {
        return false;
    }

    client.screen.stonecutter = pos;
    client.connection->send(QcMsg::OpenStonecutter{pos});
    return true;
}

void QcServerStonecutter::close(QcServerClient &client, bool notify)
{
    if (!client.screen.stonecutter) {
        return;
    }
    client.screen.stonecutter.reset();
    if (notify) {
        client.connection->send(QcMsg::CloseStonecutter{});
    }
}

bool QcServerStonecutter::canStayOpen(const QcServerClient &client) const
{
    const std::optional<QcBlockPos> pos = client.screen.stonecutter;
    if (!pos || client.dead) {
        return false;
    }
    const std::optional<QcBlockId> block = m_state.world().blockAt(*pos);
    return block && m_state.content().isValidBlock(*block) && m_state.content().block(*block).opensStonecutter
           && QcLineOfSight::isWithinReach(client, *pos);
}

void QcServerStonecutter::update()
{
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing && client->screen.stonecutter && !canStayOpen(*client)) {
            close(*client, true);
        }
    }
}

bool QcServerStonecutter::cutOnce(QcServerClient &client, const QcSmeltingRecipe &recipe) const
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcItemId> input = content.itemId(recipe.inputItem);
    const std::optional<QcItemId> result = content.itemId(recipe.resultItem);
    if (!input || !result) {
        return false;
    }

    // All or nothing, on a copy: a full inventory leaves everything as it was.
    QcInventory attempt = client.inventory;
    if (!attempt.removeItems(*input, 1) || attempt.add(*result, recipe.resultCount, content) > 0) {
        return false;
    }
    client.inventory = attempt;
    return true;
}

void QcServerStonecutter::cut(QcServerClient &client, const QcMsg::CutStone &request)
{
    const std::optional<QcBlockPos> at = client.screen.stonecutter;
    if (!at || !canStayOpen(client)) {
        close(client, true);
        return;
    }

    const QcContentRegistry &content = m_state.content();
    const std::optional<qsizetype> index = content.stonecuttingIndex(request.recipe);
    if (!index) {
        m_inventory.send(client);
        return;
    }

    const QcSmeltingRecipe &recipe = content.stonecuttingRecipes()[*index];
    const int rounds = request.repeat ? QcMsg::CutStone::maxRepeats : 1;
    int done = 0;
    while (done < rounds && cutOnce(client, recipe)) {
        ++done;
    }
    if (done > 0) {
        // canStayOpen checked that a stonecutter stands there; it sounds like itself being struck.
        const QcBlockId block = m_state.world().blockAt(*at).value_or(qcAirBlockId);
        m_sounds.playBlock(content.block(block), QcBlockSoundAction::Hit, *at);
    }
    m_inventory.send(client);
}
