// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/villages/QcTradeDefinitions.h"
#include "common/gameplay/QcItemStack.h"
#include "server/villages/QcVillager.h"

#include <QHash>
#include <QList>

#include <utility>

class QcContentRegistry;
struct QcVillagerSettings;

/*! \brief The offers of the villagers (decision E-414): the offers of each profession from the
    trade sets of the content (QcTradeSet; offers naming an unknown item are left out with a
    warning), which of them a villager has, their price, the villager's experience and level, and
    restocking.

    The trading settings are those of one villager species (QcVillagerSettings::trading, decision
    E-421); a profession's own offersPerLevel takes the place of the species'.

    A villager has, for each level it reached, up to QcTradingSettings::offersPerLevel offers of
    that level: those of its record that still exist, the free places drawn from its profession's
    other offers of the level by a stable hash of its look, the level and the offer's id. So it
    keeps them for good (also when a mod adds or removes offers), two villagers of a profession
    can differ, and the draw is the same on every Qt version. Its record keeps the uses and
    demand of each by the offer's id (QcVillagerRecord::offers). */
class QcVillagerTrades
{
public:
    /*! \brief An offer with its items resolved; \c {costs} at the base price.

        The first cost never rises beyond \c {firstMaxStack}, the stack size of its item. */
    struct Offer
    {
        /*! The offer of the content. */
        QcTradeOffer definition;
        /*! Its costs at the base price. */
        QList<QcItemStack> costs;
        /*! What it gives. */
        QcItemStack result;
        /*! The stack size of the first cost's item. */
        int firstMaxStack = 64;
    };

    QcVillagerTrades() = default;
    QcVillagerTrades(const QcContentRegistry &content, const QcVillagerSettings &species);

    /*! Returns the trading settings of the species. */
    const QcTradingSettings &settings() const { return m_settings; }
    /*! Returns all offers of \a profession, by level and then in the order of the content. */
    QList<Offer> offersOfProfession(const QString &profession) const { return m_offers.value(profession); }
    /*! Returns the offers of \a villager by its profession, level and look, those of level 1
        first. */
    QList<const Offer *> offersOf(const QcVillagerRecord &villager) const;
    /*! Aligns the saved state of \a villager with offersOf: known ids keep their uses and demand,
        new offers start fresh, others are dropped. */
    void sync(QcVillagerRecord &villager) const;
    /*! Returns the costs of \a offer at its current price with the saved \a state: demand raises
        the first cost by \c {demand} items a step, at most to twice its base count and to its stack
        size. */
    QList<QcItemStack> priceOf(const Offer &offer, const QcVillagerOffer &state) const;

    /*! Returns the experience of \a level and of the next (the same at the highest level). */
    std::pair<int, int> levelRange(int level) const;
    /*! Adds \a points of trade experience to \a villager; returns the levels reached by it
        (offersOf grows, sync follows). */
    int addExperience(QcVillagerRecord &villager, int points) const;
    /*! \a villager at its workstation during work at \a worldTime: restocks when an offer was
        used, the day's restocks are not spent and the last one lies restockGap ago. Sold-out
        offers rise in demand, the others fall; all uses start again (after sync). Returns
        whether it restocked; cheap while no offer was used. */
    bool restock(QcVillagerRecord &villager, double worldTime) const;

private:
    // The offers of `level` from its `pool` (the profession's offers of the level) the villager has.
    QList<const Offer *> drawn(const QcVillagerRecord &villager, int level, const QList<const Offer *> &pool) const;

    QcTradingSettings m_settings;
    // Profession -> its own offers per level.
    QHash<QString, int> m_offersPerLevel;
    QHash<QString, QList<Offer>> m_offers;
};
