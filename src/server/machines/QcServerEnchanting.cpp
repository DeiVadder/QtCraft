#include "server/machines/QcServerEnchanting.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/content/registry/QcContentRegistry.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/machines/QcBlockScreens.h"

#include <utility>

using namespace Qt::StringLiterals;

QcServerEnchanting::QcServerEnchanting(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
    , m_screens(screens)
{
    // One screen at a time: a block's, cart's or villager's screen opening closes the table.
    m_screens.setEnchantingCloser([this](QcServerClient &client) { close(client, true); });
}

bool QcServerEnchanting::isTable(const QcServerState &state, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = state.world().blockAt(pos);
    return block && state.content().isValidBlock(*block) && state.content().block(*block).opensEnchanting;
}

bool QcServerEnchanting::open(QcServerClient &client, QcBlockPos pos)
{
    if (!isTable(m_state, pos)) {
        return false;
    }

    const bool same = client.screen.enchanting && client.screen.enchanting->pos == pos;
    m_screens.close(client, true);
    m_screens.endTrade(client);
    m_screens.closeStonecutter(client);
    m_screens.closeAnvil(client);
    if (!same) {
        m_inventory.returnScreenInputs(client);
        client.screen.enchanting = QcServerClient::Enchanting{pos, {}, {}, {}, -1};
    }
    client.connection->send(QcMsg::OpenEnchanting{pos});
    m_inventory.send(client);
    return true;
}

void QcServerEnchanting::close(QcServerClient &client, bool notify)
{
    if (!client.screen.enchanting) {
        return;
    }
    m_inventory.returnScreenInputs(client);
    client.screen.enchanting.reset();
    if (client.state == QcServerClient::State::Closed) {
        return;
    }
    if (notify) {
        client.connection->send(QcMsg::CloseEnchanting{});
    }
    m_inventory.send(client);
}

bool QcServerEnchanting::canStayOpen(const QcServerClient &client) const
{
    return client.screen.enchanting && !client.dead && client.rules().interacts && isTable(m_state, client.screen.enchanting->pos)
           && QcLineOfSight::isWithinReach(client, client.screen.enchanting->pos);
}

void QcServerEnchanting::update()
{
    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing || !client->screen.enchanting) {
            continue;
        }
        if (!canStayOpen(*client)) {
            close(*client, true);
        } else if (shelvesAt(m_state, client->screen.enchanting->pos) != client->screen.enchanting->shownShelves) {
            // Someone placed or broke a bookshelf: the offers change, so the client hears them.
            m_inventory.send(*client);
        }
    }
}

int QcServerEnchanting::shelvesAt(const QcServerState &state, QcBlockPos pos)
{
    const QcContentRegistry &content = state.content();
    const std::optional<QcBlockId> shelf = content.blockId(u"qtcraft:bookshelf"_s);
    const auto isShelf = [&state, shelf](QcBlockPos at) { return shelf && state.world().blockAt(at) == shelf; };
    const auto isFree = [&state, &content](QcBlockPos at) {
        const std::optional<QcBlockId> block = state.world().blockAt(at);
        return block && (*block == qcAirBlockId || (content.isValidBlock(*block) && content.block(*block).replaceable));
    };
    return QcEnchantingTable::shelvesAround(pos, isShelf, isFree);
}

QcEnchantingTable::Offers QcServerEnchanting::offersFor(QcServerState &state, QcServerClient &client)
{
    if (!client.screen.enchanting) {
        return {};
    }
    while (client.enchantSeed == 0) {
        client.enchantSeed = state.random().generate();
    }
    const QcServerClient::Enchanting &table = *client.screen.enchanting;
    return QcEnchantingTable::offers(state.content(), table.item, shelvesAt(state, table.pos), client.enchantSeed);
}

QcMsg::EnchantingState QcServerEnchanting::stateOf(QcServerState &state, QcServerClient &client)
{
    QcMsg::EnchantingState message;
    if (!client.screen.enchanting) {
        return message;
    }
    message.item = client.screen.enchanting->item;
    message.lapis = client.screen.enchanting->lapis;
    message.shelves = shelvesAt(state, client.screen.enchanting->pos);
    const QcEnchantingTable::Offers offers = offersFor(state, client);
    client.screen.enchanting->shown = offers;
    client.screen.enchanting->shownShelves = message.shelves;
    for (std::size_t slot = 0; slot < offers.size(); ++slot) {
        const QcEnchantingTable::Offer &offer = offers[slot];
        if (offer.levelCost > 0) {
            message.offers[slot] = {offer.levelCost, offer.enchantments.constFirst().name, offer.enchantments.constFirst().level};
        }
    }
    return message;
}

void QcServerEnchanting::choose(QcServerClient &client, QcMsg::ChooseEnchantment request)
{
    if (!client.screen.enchanting || !canStayOpen(client)) {
        close(client, true);
        return;
    }

    const int slot = std::clamp(static_cast<int>(request.slot), 0, QcEnchantingTable::offerCount - 1);
    QcServerClient::Enchanting &table = *client.screen.enchanting;
    // Only the offer the client saw counts: when the offers changed since (shelves, item), the
    // client gets the new ones and chooses again.
    const QcEnchantingTable::Offers current = offersFor(m_state, client);
    if (current != table.shown) {
        m_inventory.send(client);
        return;
    }
    const QcEnchantingTable::Offer &offer = current[static_cast<std::size_t>(slot)];
    const int price = QcEnchantingTable::priceOf(slot);
    // Creative players pay nothing (the reference's rule); others need the offer's level, and pay
    // levels and lapis by the offer's place.
    const bool free = client.rules().itemCatalogue;
    const bool affordable = free || (client.experience.level() >= offer.levelCost && !table.lapis.isEmpty() && table.lapis.count >= price);
    // The enchantments must fit the item data (QcItemData::fitsLimits) before anything is paid.
    QcItemData data = table.item.itemData();
    data.enchantments = offer.enchantments;
    const std::optional<QcItemStack> enchanted = offer.levelCost > 0 && affordable ? table.item.tryWithData(data) : std::nullopt;
    if (!enchanted) {
        m_inventory.send(client);
        return;
    }

    if (!free) {
        [[maybe_unused]] const bool paid = client.experience.spendLevels(price);
        table.lapis = table.lapis.count > price ? table.lapis.withCount(table.lapis.count - price) : QcItemStack();
    }
    // A book becomes an enchanted book (decision E-873).
    table.item = *enchanted;
    table.item.item = QcEnchantingTable::enchantedItem(m_state.content(), enchanted->item);
    client.enchantSeed = 0;
    m_sounds.play(QString::fromLatin1(QcSoundKeys::levelUp), QcVec3{table.pos.x + 0.5, table.pos.y + 0.5, table.pos.z + 0.5});
    m_inventory.send(client);
}
