// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"
#include "server/mobs/QcDimensionDepartures.h"

#include <QList>
#include <QString>

class QcContentRegistry;
class QcServerInventory;
class QcServerSounds;
class QcServerState;
struct QcBarterDefinition;
struct QcBarterState;
struct QcCreature;
struct QcItemEntity;
struct QcServerClient;

/*! \brief Creatures that love gold and barter (QcMobDefinition::barter, decision E-1081; the
    tuskling), server authoritative.

    A player gives one \c {currency} item from his hand (use on the creature, give()), or the
    creature walks to a loved item lying within its \c {seekRange}, even away from a foe, and
    picks one up (tick()); one it does not reach in seekLimitTicks is left alone for ignoreTicks.
    It looks at the item for \c {admireSeconds} (QcBarterState, the item shown in its hand) and
    stands still; then the \c {currency} is bartered away for one draw of its loot table
    (QcContentLoot::draw()), thrown towards the player who gave it, and any other loved item goes
    to its hoard. A taken item stays the stack it was (damage, enchantments, name). Taking an
    item keeps the creature with the world. Anger ends the looking: the item falls to the ground
    and nothing is bartered; an angry creature takes nothing. Breaking or opening a block it
    guards angers it (alarm()). Young ones neither take nor barter. Hoard and looked-at item fall
    at every death (dropsOf()). */
class QcServerBarter
{
public:
    /*! Game ticks between two looks of a creature for loved items lying around (staggered by
        its id). */
    static constexpr int seekTicks = 10;
    /*! A loved item this close to the middle of its feet (metres) is picked up. */
    static constexpr double pickupReach = 1.2;
    /*! Game ticks it walks to one item before it gives up (10 s, as in the role model). */
    static constexpr quint64 seekLimitTicks = 200;
    /*! Game ticks it leaves an item alone that it gave up on (10 s, as in the role model). */
    static constexpr quint64 ignoreTicks = 200;

    /*! \brief What became of an item offered to a creature. */
    enum class Giving : quint8 {
        /*! Not a creature that barters or not its currency: nothing happened. */
        NotTheCurrency,
        /*! Its currency, but it does not take it now (young, busy, angry, out of reach or
            sight). */
        Refused,
        /*! It took one. */
        Given
    };

    QcServerBarter(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    /*! Handles a use of \a client with his held item on the creature \a mobId and returns what
        became of it. */
    Giving give(QcServerClient &client, quint32 mobId);
    /*! Angers the creatures that barter at \a client because he broke (\a opened false) or
        opened (\a opened true) a block named \a block: every awake adult of a kind
        that guards it within its \c {alarmRange} of the player, when opened only those that see
        him. */
    void alarm(const QcServerClient &client, const QString &block, bool opened);
    /*! The looking, bartering and picking up of one game tick. */
    void tick();
    /*! Returns what a creature holding \a barter drops when it dies: its hoard and the item it
        looks at, as the stacks they are. */
    static QList<QcItemStack> dropsOf(const QcBarterState &barter);

private:
    // Starts looking at `stack`, given by the player `from` (0 for none); keeps the creature with
    // the world.
    void take(QcCreature &mob, const QcBarterDefinition &barter, const QcItemStack &stack, quint32 from);
    // Barters the item it looked at or keeps it; an angry one drops it instead.
    void finish(QcCreature &mob, const QcBarterDefinition &barter);
    // Walks to the nearest loved item lying around and picks it up when it reached it; gives up on
    // one it does not reach in time.
    void seek(QcCreature &mob, const QcBarterDefinition &barter);
    // The nearest item lying around that `mob` wants and has not given up on; nullptr for none.
    const QcItemEntity *nearestLoved(const QcCreature &mob, const QcBarterDefinition &barter) const;
    // Throws `stacks` from the creature's front towards the player `towards` (or ahead of it).
    void throwOut(const QcCreature &mob, const QList<QcItemStack> &stacks, quint32 towards);
    // Whether `mob` may take an item now: awake, adult, not angry, not looking at one. Chasing a
    // foe is no reason against it: only a blow keeps it from gold, as in the role model.
    static bool mayTake(const QcCreature &mob);
    // A creature that died lets fall what it holds, wherever it died.
    void dropHeld(const QcCreature &mob);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcDimensionDepartures m_departures;
};
