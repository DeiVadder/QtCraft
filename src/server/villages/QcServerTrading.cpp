#include "server/villages/QcServerTrading.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/world/QcDayCycle.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/items/QcServerExperience.h"
#include "server/machines/QcBlockScreens.h"
#include "server/mobs/QcMobSteering.h"
#include "server/mobs/QcServerAnimals.h"
#include "server/villages/QcVillagerBehaviour.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// The player walks a little beyond the reach that opened the screen before it closes.
constexpr double keepSlack = 1.0;

QcVec3 centreOf(const QcCreature &mob, const QcMobDefinition &definition)
{
    return mob.motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
}

// Whether the inventory holds all costs (two costs of the same item add up).
bool canPay(const QcInventory &inventory, const QList<QcItemStack> &costs)
{
    return std::ranges::all_of(costs, [&](const QcItemStack &cost) {
        int needed = 0;
        for (const QcItemStack &other : costs) {
            needed += other.item == cost.item ? other.count : 0;
        }
        return inventory.countOf(cost.item) >= needed;
    });
}

} // namespace

QcServerTrading::QcServerTrading(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
    , m_screens(screens)
{
}

const QcVillagerTrades &QcServerTrading::trades(const QcCreature &villager) const
{
    return m_state.entities().creatures().villagers().trades(villager.type);
}

bool QcServerTrading::inReach(const QcServerClient &client, const QcCreature &villager, double slack) const
{
    const QcMobDefinition &definition = m_state.content().mobs()[villager.type];
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    return (centreOf(villager, definition) - eye).length() <= QcServerCombat::attackReach + definition.width + 1.0 + slack;
}

bool QcServerTrading::open(QcServerClient &client, quint32 id)
{
    // A sneaking player uses the held item on it (food, the catching item) instead; a villager behind
    // a wall is not reached.
    QcCreature *villager = m_state.entities().creatures().find(id);
    if (!villager || client.sneaking || !m_state.content().mobs()[villager->type].villager || !inReach(client, *villager, 0.0)
        || !QcLineOfSight::canSee(m_state.world(), client, QcServerAnimals::shownBox(m_state.content().mobs()[villager->type], *villager))) {
        return false;
    }

    // Asleep, on its way to bed, fleeing, trading with another player or a child: it only looks up.
    const QcMobDefinition &definition = m_state.content().mobs()[villager->type];
    const bool busy = villager->villager->trader != 0 && villager->villager->trader != client.id;
    const QcVillagerSettings &species = m_state.content().villagerSettingsOf(villager->type);
    const bool bedtime = species.activityAt(QcDayCycle::phase(m_state.info().worldTime), villager->villager->profession) == QcVillagerActivity::Sleep;
    trades(*villager).sync(*villager->villager);
    if (villager->pose == QcPose::Lying || bedtime || villager->fleeTimer > 0.0 || busy || client.dead || villager->isYoung()
        || villager->villager->offers.isEmpty()) {
        if (villager->pose != QcPose::Lying) {
            villager->yaw = QcMobSteering::yawTowards(client.motion.position - villager->motion.position);
        }
        if (!definition.ambientSound.isEmpty()) {
            m_sounds.play(definition.ambientSound, centreOf(*villager, definition));
        }
        return true;
    }

    close(client, false);
    // One screen at a time: an open machine, chest, cart or stonecutter closes.
    m_screens.close(client, true);
    m_screens.closeStonecutter(client);
    m_screens.closeEnchanting(client);
    villager->villager->trader = client.id;
    villager->villager->traderAt = client.motion.position;
    client.screen.trading = id;
    m_trading.insert(id);
    send(client, *villager, true);
    return true;
}

QcCreature *QcServerTrading::partnerOf(const QcServerClient &client)
{
    QcCreature *villager = client.screen.trading != 0 ? m_state.entities().creatures().find(client.screen.trading) : nullptr;
    if (!villager || villager->villager->trader != client.id || villager->pose == QcPose::Lying || villager->dormant || villager->health <= 0.0) {
        return nullptr;
    }
    return villager;
}

void QcServerTrading::trade(QcServerClient &client, QcMsg::Trade message)
{
    QcCreature *villager = partnerOf(client);
    if (!villager || villager->id != message.villager || client.dead || message.offer < 0) {
        return;
    }
    // The saved state follows the offers first (a command may have changed the profession).
    trades(*villager).sync(*villager->villager);
    const QList<const QcVillagerTrades::Offer *> offers = trades(*villager).offersOf(*villager->villager);
    if (message.offer >= offers.size()) {
        send(client, *villager, true);
        return;
    }

    const QcVillagerTrades::Offer offer = *offers[message.offer];
    const int levelBefore = villager->villager->level;
    int traded = 0;
    while (traded < (message.repeat ? maxRepeats : 1) && tradeOnce(client, *villager, offer, message.offer)) {
        ++traded;
    }
    if (traded == 0) {
        send(client, *villager, true);
        return;
    }

    hand(client, *villager, offer, traded);
    const QcMobDefinition &definition = m_state.content().mobs()[villager->type];
    if (villager->villager->level > levelBefore) {
        trades(*villager).sync(*villager->villager);
        m_sounds.play(QString::fromLatin1(QcSoundKeys::levelUp), centreOf(*villager, definition));
    } else if (!definition.ambientSound.isEmpty()) {
        m_sounds.play(definition.ambientSound, centreOf(*villager, definition));
    }
    m_inventory.send(client);
    send(client, *villager, true);
}

bool QcServerTrading::tradeOnce(QcServerClient &client, QcCreature &villager, const QcVillagerTrades::Offer &offer, qsizetype index)
{
    QcVillager &record = *villager.villager;
    if (index >= record.offers.size() || record.offers[index].id != offer.definition.id) {
        return false;
    }
    QcVillagerOffer &state = record.offers[index];
    const QList<QcItemStack> costs = trades(villager).priceOf(offer, state);
    if (state.uses >= offer.definition.maxUses || !canPay(client.inventory, costs)) {
        return false;
    }

    for (const QcItemStack &cost : costs) {
        [[maybe_unused]] const bool paid = client.inventory.removeItems(cost.item, cost.count);
        Q_ASSERT(paid);
    }
    ++state.uses;
    trades(villager).addExperience(record, offer.definition.villagerExperience);
    return true;
}

void QcServerTrading::hand(QcServerClient &client, const QcCreature &villager, const QcVillagerTrades::Offer &offer, int traded)
{
    // The results of all trades at once, in whole stacks: what does not fit falls as few drops.
    const int maxStack = std::max(1, m_state.content().item(offer.result.item).maxStack);
    for (int left = offer.result.count * traded; left > 0; left -= maxStack) {
        m_inventory.giveOrThrow(client, offer.result.withCount(std::min(left, maxStack)));
    }

    // The player's experience falls as orbs between the villager and the player, all at once.
    const QcVec3 between = (villager.motion.position + client.motion.position) * 0.5 + QcVec3{0.0, 0.5, 0.0};
    QcServerExperience::drop(m_state, offer.definition.playerExperience * traded, between);
}

void QcServerTrading::close(QcServerClient &client, bool notify)
{
    if (client.screen.trading == 0) {
        return;
    }

    if (QcCreature *villager = m_state.entities().creatures().find(client.screen.trading); villager && villager->villager->trader == client.id) {
        villager->villager->trader = 0;
        villager->villager->decideIn = 0;
    }
    m_trading.remove(client.screen.trading);
    client.screen.trading = 0;
    client.screen.sentTrade.reset();
    if (notify) {
        client.connection->send(QcMsg::CloseTrade{});
    }
}

void QcServerTrading::update()
{
    if (m_trading.isEmpty()) {
        return;
    }

    QSet<quint32> traded;
    for (const auto &client : m_state.clients()) {
        if (client->screen.trading == 0) {
            continue;
        }
        QcCreature *villager = partnerOf(*client);
        if (client->state != QcServerClient::State::Playing || client->dead || !villager || !inReach(*client, *villager, keepSlack)) {
            close(*client, true);
            continue;
        }
        villager->villager->traderAt = client->motion.position;
        traded.insert(villager->id);
        send(*client, *villager, false);
    }

    // Villagers whose trader left the game stop trading.
    for (const quint32 id : std::as_const(m_trading)) {
        QcCreature *villager = traded.contains(id) ? nullptr : m_state.entities().creatures().find(id);
        if (villager) {
            villager->villager->trader = 0;
        }
    }
    m_trading = std::move(traded);
}

QcMsg::TradeState QcServerTrading::stateOf(const QcCreature &villager) const
{
    const QcVillager &record = *villager.villager;
    const QcVillagerProfession *profession = m_state.content().villageCatalog().profession(record.profession);
    const auto [start, end] = trades(villager).levelRange(record.level);
    QcMsg::TradeState state{villager.id, profession ? profession->translationKey : QString(), record.level, record.experience, start, end, {}};

    const QList<const QcVillagerTrades::Offer *> offers = trades(villager).offersOf(record);
    // Each offer with its state by id (a fresh one until sync adds it).
    for (qsizetype i = 0; i < offers.size() && i < QcMsg::TradeState::maxOffers; ++i) {
        const auto known = std::ranges::find(record.offers, offers[i]->definition.id, &QcVillagerOffer::id);
        const QcVillagerOffer offer = known != record.offers.cend() ? *known : QcVillagerOffer{offers[i]->definition.id, 0, 0};
        state.offers.append({trades(villager).priceOf(*offers[i], offer), offers[i]->result, offer.uses, offers[i]->definition.maxUses});
    }
    return state;
}

void QcServerTrading::send(QcServerClient &client, const QcCreature &villager, bool force)
{
    QcMsg::TradeState state = stateOf(villager);
    if (!force && client.screen.sentTrade == state) {
        return;
    }
    client.screen.sentTrade = state;
    client.connection->send(std::move(state));
}
