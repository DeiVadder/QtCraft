#include "common/gameplay/QcEnchantingTable.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcEnchanting.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

namespace {

// A candidate of an offer: an enchantment at the highest level the power reaches.
struct Candidate
{
    const QcEnchantmentDefinition *definition = nullptr;
    int level = 0;
};

// The applicable enchantments with the highest level whose power range holds `power`.
QList<Candidate> candidatesFor(const QcContentRegistry &content, const QcItemDefinition &item, int power)
{
    QList<Candidate> candidates;
    for (const QcEnchantmentDefinition &definition : content.enchantments()) {
        if (!QcEnchanting::appliesTo(definition, item)) {
            continue;
        }
        for (int level = definition.maxLevel; level >= 1; --level) {
            if (power >= definition.lowestPower(level) && power <= definition.highestPower(level)) {
                candidates.append({&definition, level});
                break;
            }
        }
    }
    return candidates;
}

// One candidate, chosen by weight.
Candidate pickWeighted(const QList<Candidate> &candidates, QRandomGenerator &random)
{
    int total = 0;
    for (const Candidate &candidate : candidates) {
        total += candidate.definition->weight;
    }
    int roll = random.bounded(total);
    for (const Candidate &candidate : candidates) {
        roll -= candidate.definition->weight;
        if (roll < 0) {
            return candidate;
        }
    }
    return candidates.constLast();
}

// The enchanting power of `levelCost` for an item with `enchantability`, varied by up to 15 %.
int powerOf(int levelCost, int enchantability, QRandomGenerator &random)
{
    const int quarter = enchantability / 4 + 1;
    const int power = levelCost + 1 + random.bounded(quarter) + random.bounded(quarter);
    const double spread = (random.generateDouble() + random.generateDouble() - 1.0) * 0.15;
    return std::max(1, static_cast<int>(std::lround(power + power * spread)));
}

} // namespace

namespace QcEnchantingTable {

int shelvesAround(QcBlockPos table, const std::function<bool(QcBlockPos)> &isShelf, const std::function<bool(QcBlockPos)> &isFree)
{
    return static_cast<int>(shelvesOf(table, isShelf, isFree).size());
}

QList<QcBlockPos> shelvesOf(QcBlockPos table, const std::function<bool(QcBlockPos)> &isShelf, const std::function<bool(QcBlockPos)> &isFree)
{
    // The 16 columns of the ring two blocks around the table (x, z).
    static constexpr std::array<std::array<int, 2>, 16> ring{{
        {-2, -2}, {-1, -2}, {0, -2}, {1, -2}, {2, -2}, {-2, 2}, {-1, 2}, {0, 2},
        {1, 2},   {2, 2},   {-2, -1}, {-2, 0}, {-2, 1}, {2, -1}, {2, 0}, {2, 1},
    }};
    QList<QcBlockPos> shelves;
    for (int dy = 0; dy <= 1; ++dy) {
        for (const auto &[dx, dz] : ring) {
            const QcBlockPos shelf{table.x + dx, table.y + dy, table.z + dz};
            const QcBlockPos between{table.x + dx / 2, table.y + dy, table.z + dz / 2};
            if (shelves.size() < maxShelves && isShelf(shelf) && isFree(between)) {
                shelves.append(shelf);
            }
        }
    }
    return shelves;
}

bool letsShelfThrough(const QcContentRegistry &content, std::optional<QcBlockId> block)
{
    return block && (*block == qcAirBlockId || (content.isValidBlock(*block) && content.block(*block).replaceable));
}

QList<QcBlockPos> shelvesIn(const QcContentRegistry &content, QcBlockPos table, const BlockAt &blockAt)
{
    const std::optional<QcBlockId> shelf = content.blockId(QStringLiteral("qtcraft:bookshelf"));
    if (!shelf) {
        return {};
    }
    const auto isShelf = [&blockAt, shelf](QcBlockPos at) { return blockAt(at) == shelf; };
    const auto isFree = [&blockAt, &content](QcBlockPos at) { return letsShelfThrough(content, blockAt(at)); };
    return shelvesOf(table, isShelf, isFree);
}

std::array<int, offerCount> levelCosts(quint32 seed, int shelves, int enchantability)
{
    std::array<int, offerCount> costs{};
    if (enchantability <= 0) {
        return costs;
    }
    shelves = std::clamp(shelves, 0, maxShelves);
    QRandomGenerator random(seed);
    for (int slot = 0; slot < offerCount; ++slot) {
        const int base = random.bounded(8) + 1 + shelves / 2 + random.bounded(shelves + 1);
        const int cost = slot == 0 ? std::max(base / 3, 1) : slot == 1 ? base * 2 / 3 + 1 : std::max(base, shelves * 2);
        costs[static_cast<std::size_t>(slot)] = cost < slot + 1 ? 0 : cost;
    }
    return costs;
}

QList<QcEnchantment> enchantmentsFor(const QcContentRegistry &content, const QcItemStack &stack, int levelCost, quint32 seed)
{
    if (stack.isEmpty() || !content.isValidItem(stack.item)) {
        return {};
    }
    const QcItemDefinition &item = content.item(enchantedItem(content, stack.item));
    QRandomGenerator random(seed);
    int power = powerOf(levelCost, content.item(stack.item).enchantability, random);
    QList<Candidate> candidates = candidatesFor(content, item, power);
    QList<QcEnchantment> chosen;
    while (!candidates.isEmpty()) {
        const Candidate picked = pickWeighted(candidates, random);
        chosen.append({picked.definition->name, picked.level});
        // Each further enchantment is less likely: the power halves after each.
        if (random.bounded(50) > power) {
            break;
        }
        candidates.removeIf([&picked](Candidate other) { return QcEnchanting::conflict(*picked.definition, *other.definition); });
        power /= 2;
    }
    return chosen;
}

Offers offers(const QcContentRegistry &content, const QcItemStack &stack, int shelves, quint32 seed)
{
    Offers result;
    // One item at a time, as in the reference: a stack of books gets no offers.
    if (stack.isEmpty() || stack.count != 1 || !content.isValidItem(stack.item) || !stack.enchantments().isEmpty()) {
        return result;
    }
    const std::array<int, offerCount> costs = levelCosts(seed, shelves, content.item(stack.item).enchantability);
    for (int slot = 0; slot < offerCount; ++slot) {
        const int cost = costs[static_cast<std::size_t>(slot)];
        QList<QcEnchantment> enchantments = cost > 0 ? enchantmentsFor(content, stack, cost, seed + static_cast<quint32>(slot)) : QList<QcEnchantment>();
        if (!enchantments.isEmpty()) {
            result[static_cast<std::size_t>(slot)] = {cost, std::move(enchantments)};
        }
    }
    return result;
}

QcItemId enchantedItem(const QcContentRegistry &content, QcItemId item)
{
    if (!content.isValidItem(item) || content.item(item).enchantsInto.isEmpty()) {
        return item;
    }
    return content.itemId(content.item(item).enchantsInto).value_or(item);
}

} // namespace QcEnchantingTable
