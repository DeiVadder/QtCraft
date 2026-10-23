#include "server/villages/QcVillagerTrades.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcDayCycle.h"
#include "server/core/QcServerLogging.h"
#include "worldgen/terrain/QcNoise.h"


#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

std::optional<QcItemStack> resolve(const QcContentRegistry &content, const QcTradeStack &stack)
{
    const std::optional<QcItemId> item = content.itemId(stack.item);
    if (!item) {
        return std::nullopt;
    }
    return QcItemStack{*item, std::min(stack.count, content.item(*item).maxStack), 0};
}

// The offer with its items, or none when one of them is unknown.
std::optional<QcVillagerTrades::Offer> resolve(const QcContentRegistry &content, const QcTradeOffer &definition)
{
    QcVillagerTrades::Offer offer{definition, {}, {}, 64};
    for (const QcTradeStack &cost : definition.costs) {
        const std::optional<QcItemStack> stack = resolve(content, cost);
        if (!stack) {
            return std::nullopt;
        }
        offer.costs.append(*stack);
    }
    const std::optional<QcItemStack> result = resolve(content, definition.result);
    if (!result) {
        return std::nullopt;
    }
    offer.result = *result;
    offer.firstMaxStack = content.item(offer.costs.constFirst().item).maxStack;
    return offer;
}

qint64 dayOf(double worldTime)
{
    // Bounded, so even a damaged time converts to a day without overflow.
    return static_cast<qint64>(std::floor(std::clamp(worldTime, -1.0, QcVillagerRecord::maxRestockTime) / QcDayCycle::dayLength));
}

} // namespace

QcVillagerTrades::QcVillagerTrades(const QcContentRegistry &content, const QcVillagerSettings &species)
    : m_settings(species.trading)
{
    for (const QcVillagerProfession &profession : species.professions) {
        if (profession.offersPerLevel > 0) {
            m_offersPerLevel.insert(profession.name, profession.offersPerLevel);
        }
    }

    // Sets in load order; an offer whose id a later set repeats is replaced in place.
    for (const QcTradeSet &set : content.villageCatalog().tradeSets()) {
        QList<Offer> &offers = m_offers[set.profession];
        for (const QcTradeOffer &definition : set.offers) {
            const std::optional<Offer> offer = resolve(content, definition);
            if (!offer) {
                qCWarning(lcTrades).noquote() << u"Trades %1: offer '%2' names an unknown item and is left out"_s.arg(set.name, definition.id);
                continue;
            }
            const auto same = std::ranges::find_if(offers, [&definition](const Offer &other) { return other.definition.id == definition.id; });
            if (same != offers.end()) {
                *same = *offer;
            } else {
                offers.append(*offer);
            }
        }
    }
    for (QList<Offer> &offers : m_offers) {
        std::ranges::stable_sort(offers, {}, [](const Offer &offer) { return offer.definition.level; });
    }
}

QList<const QcVillagerTrades::Offer *> QcVillagerTrades::offersOf(const QcVillagerRecord &villager) const
{
    QList<const Offer *> chosen;
    const auto offers = m_offers.constFind(villager.profession);
    if (offers == m_offers.constEnd()) {
        return chosen;
    }

    for (int level = 1; level <= villager.level; ++level) {
        QList<const Offer *> pool;
        for (const Offer &offer : *offers) {
            if (offer.definition.level == level) {
                pool.append(&offer);
            }
        }
        chosen += drawn(villager, level, pool);
    }
    return chosen.mid(0, QcVillagerRecord::maxOffers);
}

QList<const QcVillagerTrades::Offer *> QcVillagerTrades::drawn(const QcVillagerRecord &villager, int level, const QList<const Offer *> &pool) const
{
    const auto saved = [&villager](const Offer *offer) {
        return std::ranges::find(villager.offers, offer->definition.id, &QcVillagerOffer::id) != villager.offers.cend();
    };
    // Each offer's rank for this villager and level: the hash of its look, the level and the
    // offer's id, so an offer added or removed by a mod leaves the others' order alone.
    const quint64 seed = qcMixSeed(villager.look, static_cast<quint64>(level));
    const auto rank = [seed](const Offer *offer) { return qcMixSeed(seed, qcNameSalt(offer->definition.id)); };

    // Saved offers stay; the free places go to the best ranked of the others.
    const int perLevel = m_offersPerLevel.value(villager.profession, m_settings.offersPerLevel);
    QList<const Offer *> picked;
    for (const Offer *offer : pool) {
        if (saved(offer) && picked.size() < perLevel) {
            picked.append(offer);
        }
    }
    QList<const Offer *> others;
    for (const Offer *offer : pool) {
        if (!picked.contains(offer)) {
            others.append(offer);
        }
    }
    std::ranges::stable_sort(others, {}, rank);
    picked += others.mid(0, std::max(0, perLevel - static_cast<int>(picked.size())));

    // In the content's order.
    QList<const Offer *> ordered;
    for (const Offer *offer : pool) {
        if (picked.contains(offer)) {
            ordered.append(offer);
        }
    }
    return ordered;
}

void QcVillagerTrades::sync(QcVillagerRecord &villager) const
{
    QList<QcVillagerOffer> states;
    const QList<const Offer *> offers = offersOf(villager);
    for (const Offer *offer : offers) {
        const auto known = std::ranges::find(std::as_const(villager.offers), offer->definition.id, &QcVillagerOffer::id);
        QcVillagerOffer state = known != villager.offers.cend() ? *known : QcVillagerOffer{offer->definition.id, 0, 0};
        state.uses = std::clamp(state.uses, 0, offer->definition.maxUses);
        states.append(state);
    }
    villager.offers = std::move(states);
}

QList<QcItemStack> QcVillagerTrades::priceOf(const Offer &offer, const QcVillagerOffer &state) const
{
    QList<QcItemStack> costs = offer.costs;
    if (!costs.isEmpty() && offer.definition.demand > 0) {
        QcItemStack &first = costs.first();
        const int rise = std::min(state.demand * offer.definition.demand, first.count);
        first.count = std::min(first.count + rise, offer.firstMaxStack);
    }
    return costs;
}

std::pair<int, int> QcVillagerTrades::levelRange(int level) const
{
    const int index = std::clamp(level, 1, static_cast<int>(m_settings.levels.size())) - 1;
    const int start = m_settings.levels.value(index);
    return {start, index + 1 < m_settings.levels.size() ? m_settings.levels.value(index + 1) : start};
}

int QcVillagerTrades::addExperience(QcVillagerRecord &villager, int points) const
{
    villager.experience = std::clamp(villager.experience + std::max(points, 0), 0, QcVillagerRecord::maxExperience);
    int gained = 0;
    while (villager.level < std::min(QcVillagerRecord::maxLevel, static_cast<int>(m_settings.levels.size()))
           && villager.experience >= m_settings.levels.value(villager.level)) {
        ++villager.level;
        ++gained;
    }
    return gained;
}

bool QcVillagerTrades::restock(QcVillagerRecord &villager, double worldTime) const
{
    if (std::ranges::none_of(villager.offers, [](const QcVillagerOffer &state) { return state.uses > 0; })) {
        return false;
    }
    const bool sameDay = villager.lastRestock >= 0.0 && dayOf(villager.lastRestock) == dayOf(worldTime);
    const int restocks = sameDay ? villager.restocks : 0;
    const bool tooSoon = sameDay && worldTime - villager.lastRestock < m_settings.restockGap * QcDayCycle::dayLength;
    if (restocks >= m_settings.restocksPerDay || tooSoon) {
        return false;
    }

    sync(villager);
    const QList<const Offer *> offers = offersOf(villager);
    for (qsizetype i = 0; i < villager.offers.size() && i < offers.size(); ++i) {
        QcVillagerOffer &state = villager.offers[i];
        const bool soldOut = state.uses >= offers[i]->definition.maxUses;
        // Demand never rises beyond what doubles the price.
        const int step = std::max(offers[i]->definition.demand, 1);
        const int most = std::min(QcVillagerOffer::maxDemand, (offers[i]->costs.value(0).count + step - 1) / step);
        state.demand = soldOut ? std::min(state.demand + 1, most) : std::max(state.demand - 1, 0);
        state.uses = 0;
    }
    villager.lastRestock = worldTime;
    villager.restocks = restocks + 1;
    return true;
}
