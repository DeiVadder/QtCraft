#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcItemStack.h"

#include <array>
#include <functional>
#include <optional>

class QcContentRegistry;

// The enchanting table's rules as in the reference game (decision E-864): bookshelves around the
// table raise the three offers' levels, and a player's enchantment seed fixes the offers until he
// enchants (then the server draws a new seed). Pure, so the offers are testable and the same on
// every run: the same seed, item and shelves give the same offers.
namespace QcEnchantingTable {

// At most this many bookshelves count.
inline constexpr int maxShelves = 15;
// The item an offer costs besides levels.
inline constexpr QLatin1StringView lapisItem("qtcraft:lapis_lazuli");
inline constexpr int offerCount = 3;

// One offer: the levels the player needs (0: no offer) and the enchantments it gives. Taking
// offer `slot` (0–2) costs slot + 1 levels and slot + 1 lapis lazuli.
struct Offer
{
    int levelCost = 0;
    QList<QcEnchantment> enchantments;

    bool operator==(const Offer &) const = default;
};
using Offers = std::array<Offer, offerCount>;

// The bookshelves that count for a table at `table`: those two blocks away on the ring around it
// (at the table's height and one above) with a free block between them and the table, at most
// maxShelves. `isShelf` and `isFree` tell what stands at a position.
[[nodiscard]] int shelvesAround(QcBlockPos table, const std::function<bool(QcBlockPos)> &isShelf, const std::function<bool(QcBlockPos)> &isFree);
// The positions of those bookshelves, at most maxShelves (the runes in the world fly from them,
// decision E-884).
[[nodiscard]] QList<QcBlockPos> shelvesOf(QcBlockPos table, const std::function<bool(QcBlockPos)> &isShelf,
                                         const std::function<bool(QcBlockPos)> &isFree);
// What stands at a position of a world (none: not loaded there).
using BlockAt = std::function<std::optional<QcBlockId>(QcBlockPos)>;
// Whether `block` lets a bookshelf's power through to the table: air or a replaceable block (tall
// grass, fire); not a position that is not loaded.
[[nodiscard]] bool letsShelfThrough(const QcContentRegistry &content, std::optional<QcBlockId> block);
// The bookshelves (qtcraft:bookshelf) that count for the table at `table` in the world `blockAt`
// tells (shelvesOf with letsShelfThrough): the one rule of the server's offers and the client's
// runes in the world (decision E-884).
[[nodiscard]] QList<QcBlockPos> shelvesIn(const QcContentRegistry &content, QcBlockPos table, const BlockAt &blockAt);
// The levels of the three offers for an item with `enchantability` (0: none) and `shelves`.
[[nodiscard]] std::array<int, offerCount> levelCosts(quint32 seed, int shelves, int enchantability);
// The three offers for `stack` (nothing for an empty stack, more than one item, one already enchanted
// or an item without enchantability or applicable enchantments). An item with `enchantsInto` (a
// book) gets the enchantments of the item it becomes (an enchanted book takes all, decision E-873).
[[nodiscard]] Offers offers(const QcContentRegistry &content, const QcItemStack &stack, int shelves, quint32 seed);
// The enchantments for `stack` at enchanting `levelCost`, drawn with `seed` (offer `slot` uses
// seed + slot).
[[nodiscard]] QList<QcEnchantment> enchantmentsFor(const QcContentRegistry &content, const QcItemStack &stack, int levelCost, quint32 seed);
// The item `item` becomes when it is enchanted: its `enchantsInto` (decision E-873), else itself.
[[nodiscard]] QcItemId enchantedItem(const QcContentRegistry &content, QcItemId item);
// Lapis lazuli and levels taken for offer `slot`.
[[nodiscard]] inline int priceOf(int slot)
{
    return slot + 1;
}

} // namespace QcEnchantingTable
