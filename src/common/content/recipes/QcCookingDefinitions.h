// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

/*! Returns whether a smelting recipe of \a kind (QcSmeltingRecipe::kind, empty: none) works in a
    machine or campfire that takes \a kinds (empty: every kind; decision E-770). */
inline bool qcAcceptsRecipeKind(const QStringList &kinds, const QString &kind)
{
    return kinds.isEmpty() || kinds.contains(kind);
}

/*! \brief A block eaten bite by bite where it stands (cake, decision E-773): using it eats one
    bite of \c {food} points; its state \c {bites} counts the bites taken, the bite after its
    last value removes the block. */
struct QcBitesDefinition
{
    /*! Food points of one bite (\c {"food"} in \c {"bites"}, 1 to QcItemDefinition::maxFood,
        default 2); 0 for a block that is not eaten. */
    int food = 0;

    /*! Returns whether the block is eaten bite by bite (\c {food} above 0). */
    bool isEdible() const { return food > 0; }
};

/*! \brief A campfire (decision E-772).

    A lit one (\c {slotCount} above 0) takes up to \c {slotCount} items with the use button that
    the smelting recipes of \c {category} and \c {kinds} turn into their results in \c {seconds}
    each (the recipe's own time does not count); the results drop beside it.
    \c {extinguishesInto} is the block it becomes when put out (a shovel, a water bucket),
    \c {lightsInto} the block an unlit one becomes when lit (flint and steel). Only
    \c {slotCount} reaches the client. */
struct QcCampfireDefinition
{
    /*! Upper limit of \c {slotCount}. */
    static constexpr int maxSlots = 4;
    /*! Upper limit of \c {seconds}. */
    static constexpr double maxSeconds = 3600.0;

    /*! Items it cooks at once (\c {"slots"}, 0 to maxSlots); 0 for an unlit campfire. */
    int slotCount = 0;
    /*! Seconds each item takes (\c {"seconds"}, QcSmeltingRecipe::minSeconds to maxSeconds). */
    double seconds = 30.0;
    /*! Smelting recipe category it cooks (\c {"category"}, default "smelting"). */
    QString category;
    /*! Recipe kinds it takes (\c {"kinds"}, 1 to 16); empty: every kind. */
    QStringList kinds;
    /*! Qualified block it becomes when put out (\c {"extinguishesInto"}); empty for none. */
    QString extinguishesInto;
    /*! Qualified block an unlit one becomes when lit (\c {"lightsInto"}); empty for none. */
    QString lightsInto;

    /*! Returns whether it cooks (\c {slotCount} above 0, a lit campfire). */
    bool cooks() const { return slotCount > 0; }
    /*! Returns whether the block is a campfire at all, lit (it cooks) or unlit (it can be
        lit). */
    bool isCampfire() const { return cooks() || !lightsInto.isEmpty(); }
};
