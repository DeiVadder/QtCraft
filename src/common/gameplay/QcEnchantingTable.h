// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcItemStack.h"

#include <array>
#include <functional>
#include <optional>

class QcContentRegistry;

/*! \brief The enchanting table's rules as in the reference game (decision E-864): bookshelves
    around the table raise the three offers' levels, and a player's enchantment seed fixes the
    offers until he enchants (then the server draws a new seed).

    Pure, so the offers are testable and the same on every run: the same seed, item and shelves
    give the same offers. */
namespace QcEnchantingTable {

/*! At most this many bookshelves count. */
inline constexpr int maxShelves = 15;
/*! The item an offer costs besides levels. */
inline constexpr QLatin1StringView lapisItem("qtcraft:lapis_lazuli");
/*! The number of offers. */
inline constexpr int offerCount = 3;

/*! \brief One offer: the levels the player needs (0: no offer) and the enchantments it gives.

    Taking offer \c {slot} (0–2) costs slot + 1 levels and slot + 1 lapis lazuli. */
struct Offer
{
    /*! The levels the player needs; 0 for no offer. */
    int levelCost = 0;
    /*! The enchantments it gives. */
    QList<QcEnchantment> enchantments;

    bool operator==(const Offer &) const = default;
};
/*! The three offers of a table. */
using Offers = std::array<Offer, offerCount>;

/*! Returns the bookshelves that count for a table at \a table: those two blocks away on the ring
    around it (at the table's height and one above) with a free block between them and the table,
    at most maxShelves. \a isShelf and \a isFree tell what stands at a position. */
[[nodiscard]] int shelvesAround(QcBlockPos table, const std::function<bool(QcBlockPos)> &isShelf, const std::function<bool(QcBlockPos)> &isFree);
/*! Returns the positions of the bookshelves that count for a table at \a table, as
    shelvesAround() finds them with \a isShelf and \a isFree, at most maxShelves (the runes in
    the world fly from them, decision E-884). */
[[nodiscard]] QList<QcBlockPos> shelvesOf(QcBlockPos table, const std::function<bool(QcBlockPos)> &isShelf,
                                         const std::function<bool(QcBlockPos)> &isFree);
/*! What stands at a position of a world (none: not loaded there). */
using BlockAt = std::function<std::optional<QcBlockId>(QcBlockPos)>;
/*! Returns whether \a block of \a content lets a bookshelf's power through to the table: air or
    a replaceable block (tall grass, fire); not a position that is not loaded. */
[[nodiscard]] bool letsShelfThrough(const QcContentRegistry &content, std::optional<QcBlockId> block);
/*! Returns the bookshelves (qtcraft:bookshelf of \a content) that count for the table at
    \a table in the world \a blockAt tells (shelvesOf with letsShelfThrough): the one rule of the
    server's offers and the client's runes in the world (decision E-884). */
[[nodiscard]] QList<QcBlockPos> shelvesIn(const QcContentRegistry &content, QcBlockPos table, const BlockAt &blockAt);
/*! Returns the levels of the three offers for an item with \a enchantability (0: none) and
    \a shelves, drawn with \a seed. */
[[nodiscard]] std::array<int, offerCount> levelCosts(quint32 seed, int shelves, int enchantability);
/*! Returns the three offers for \a stack with \a shelves, drawn with \a seed from the
    enchantments of \a content (nothing for an empty stack, more than one item, one
    already enchanted or an item without enchantability or applicable enchantments). An item with
    \c {enchantsInto} (a book) gets the enchantments of the item it becomes (an enchanted book
    takes all, decision E-873). */
[[nodiscard]] Offers offers(const QcContentRegistry &content, const QcItemStack &stack, int shelves, quint32 seed);
/*! Returns the enchantments of \a content for \a stack at enchanting \a levelCost, drawn with
    \a seed (offer \c {slot} uses seed + slot). */
[[nodiscard]] QList<QcEnchantment> enchantmentsFor(const QcContentRegistry &content, const QcItemStack &stack, int levelCost, quint32 seed);
/*! Returns the item \a item of \a content becomes when it is enchanted: its \c {enchantsInto}
    (decision E-873), else itself. */
[[nodiscard]] QcItemId enchantedItem(const QcContentRegistry &content, QcItemId item);
/*! Returns the lapis lazuli and the levels taken for offer \a slot. */
[[nodiscard]] inline int priceOf(int slot)
{
    return slot + 1;
}

} // namespace QcEnchantingTable
