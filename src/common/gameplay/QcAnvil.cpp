#include "common/gameplay/QcAnvil.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcNameText.h"
#include "common/gameplay/QcEnchanting.h"

#include <algorithm>
#include <optional>

namespace QcAnvil {

namespace {

// The highest penalty counted, so that sums of costs never overflow.
constexpr int maxPenalty = 1'000'000;

// What the right stack does to the left one, before the name and the penalty: the output's damage
// and, after a merge, its enchantments (the data is built once in result()).
struct Work
{
    QcItemStack output;
    int cost = 0;
    int rightUsed = 0;
    std::optional<QList<QcEnchantment>> enchantments = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
    // A swap (decision E-870) leaves the prior work as it is, so swapped stacks stack with fresh ones.
    bool keepsPriorWork = false;
};

// The anvil swap of `left` paid with `right` (decision E-870): the whole stack becomes the other
// item for one level per started swapLevelItems items, using up one `with`; nothing when the right
// stack is no `with` or the other item holds fewer.
std::optional<Work> swapWork(const QcContentRegistry &content, const QcItemStack &left, const QcItemStack &right)
{
    const std::optional<QcAnvilSwap> &swap = content.item(left.item).anvilSwap;
    if (!swap || right.isEmpty() || !content.isValidItem(right.item) || content.item(right.item).name != swap->with) {
        return std::nullopt;
    }
    const std::optional<QcItemId> into = content.itemId(swap->into);
    if (!into || content.item(*into).maxStack < left.count) {
        return std::nullopt;
    }
    QcItemStack swapped = left;
    swapped.item = *into;
    return Work{swapped, swapLevels(left.count), 1, std::nullopt, true};
}

// Whether `right` is the material that repairs `item` (QcItemDefinition::repairWith).
bool repairs(const QcItemDefinition &item, const QcItemDefinition &right)
{
    if (item.repairWith.isEmpty()) {
        return false;
    }
    return item.repairWith.startsWith(u'#') ? right.tags.contains(item.repairWith.mid(1)) : right.name == item.repairWith;
}

// Repair with the material: each item takes a quarter of the durability off the damage.
Work repairWithMaterial(const QcItemDefinition &item, const QcItemStack &left, const QcItemStack &right)
{
    const int share = std::max(1, item.durability / 4);
    const int needed = (left.damage + share - 1) / share;
    const int used = std::min(right.count, needed);
    QcItemStack repaired = left;
    repaired.damage = std::max(0, left.damage - used * share);
    return {repaired, used, used};
}

// Enchantments after merging and the levels the merge costs.
struct Merge
{
    QList<QcEnchantment> enchantments;
    int cost = 0;
};

// The enchantments of `right` merged into `left`'s for `item`.
Merge mergeEnchantments(const QcContentRegistry &content, const QcItemDefinition &item, QList<QcEnchantment> merged, const QList<QcEnchantment> &right)
{
    int cost = 0;
    for (const QcEnchantment &incoming : right) {
        const QcEnchantmentDefinition *definition = content.enchantment(incoming.name);
        if (!definition) {
            continue;
        }
        const auto conflicts = [&content, definition, &incoming](const QcEnchantment &present) {
            const QcEnchantmentDefinition *other = content.enchantment(present.name);
            return present.name != incoming.name && other && QcEnchanting::conflict(*definition, *other);
        };
        if (!QcEnchanting::appliesTo(*definition, item) || std::ranges::any_of(merged, conflicts)) {
            ++cost;
            continue;
        }

        const auto same = std::ranges::find(merged, incoming.name, &QcEnchantment::name);
        int level = incoming.level;
        if (same != merged.end()) {
            level = same->level == incoming.level ? std::min(incoming.level + 1, definition->maxLevel) : std::max(same->level, incoming.level);
            same->level = level;
        } else {
            merged.append({incoming.name, std::min(level, definition->maxLevel)});
        }
        cost += level * costPerLevel(*definition);
    }
    return {merged, cost};
}

// Combining with a second item of the same kind: the durability of both plus 12 %, then the
// right item's enchantments. Nothing fits when it neither repairs nor merges anything, so a right
// item whose enchantments all conflict is not used up for a rename.
Work combine(const QcContentRegistry &content, const QcItemDefinition &item, const QcItemStack &left, const QcItemStack &right)
{
    Work work{left, 0, 1};
    const bool repaired = item.durability > 0 && left.damage > 0;
    if (repaired) {
        const int remaining = (item.durability - left.damage) + (item.durability - right.damage) + item.durability * 12 / 100;
        work.output.damage = std::max(0, item.durability - remaining);
        work.cost += 2;
    }

    const Merge merge = mergeEnchantments(content, item, left.enchantments(), right.enchantments());
    const bool merged = merge.enchantments != left.enchantments();
    if (!repaired && !merged) {
        return {};
    }
    work.cost += merge.cost;
    if (merged) {
        work.enchantments = merge.enchantments;
    }
    return work;
}

// What `right` does to `left`; an empty output when it does not fit.
Work workOn(const QcContentRegistry &content, const QcItemStack &left, const QcItemStack &right)
{
    if (right.isEmpty()) {
        return {left, 0, 0};
    }
    if (std::optional<Work> swapped = swapWork(content, left, right)) {
        return *std::move(swapped);
    }
    if (left.count != 1 || !content.isValidItem(right.item)) {
        return {};
    }
    const QcItemDefinition &item = content.item(left.item);
    if (item.durability > 0 && left.damage > 0 && repairs(item, content.item(right.item))) {
        return repairWithMaterial(item, left, right);
    }
    if (right.item == left.item && right.count == 1 && (item.durability > 0 || !right.enchantments().isEmpty())) {
        return combine(content, item, left, right);
    }
    return {};
}

} // namespace

Result result(const QcContentRegistry &content, const QcItemStack &left, const QcItemStack &right, const QString &name)
{
    if (left.isEmpty() || !content.isValidItem(left.item) || left.hasContents()) {
        return {};
    }
    Work work = workOn(content, left, right);
    if (work.output.isEmpty()) {
        return {};
    }

    // The output's data is built once: merged enchantments, the name and the prior work. Data
    // beyond the limits of QcItemData gives no output, rather than an output that keeps old data.
    QcItemData data = left.itemData();
    if (work.enchantments) {
        data.enchantments = *work.enchantments;
    }

    // Blank text takes a custom name off; the stack's own name leaves it as it is.
    const QString wanted = nameFrom(name);
    if (wanted != left.name()) {
        data.name = wanted;
        ++work.cost;
    }
    if (!right.isEmpty() && !work.keepsPriorWork) {
        data.priorWork = std::clamp(std::max(left.priorWork(), right.priorWork()) + 1, 0, QcItemData::maxPriorWork);
    }
    const std::optional<QcItemStack> output = work.output.tryWithData(data);
    if (!output || work.cost == 0 || *output == left) {
        return {};
    }

    const int penalty = std::min(penaltyOf(left.priorWork()) + penaltyOf(right.isEmpty() ? 0 : right.priorWork()), maxPenalty);
    int levels = work.cost + penalty;
    if (right.isEmpty()) {
        // A rename alone never becomes too expensive (the reference's rule).
        levels = std::min(levels, tooExpensive - 1);
    }
    return {*output, levels, work.rightUsed};
}

int penaltyOf(int uses)
{
    const int clamped = std::clamp(uses, 0, 20);
    return (1 << clamped) - 1;
}

int swapLevels(int count)
{
    return std::max(1, (count + swapLevelItems - 1) / swapLevelItems);
}

int costPerLevel(const QcEnchantmentDefinition &enchantment)
{
    if (enchantment.weight >= 10) {
        return 1;
    }
    if (enchantment.weight >= 5) {
        return 2;
    }
    return enchantment.weight >= 2 ? 4 : 8;
}

QString nameFrom(const QString &text)
{
    // QcNameText counts code points, QcItemData's limit UTF-16 units: fit again with fewer code
    // points until the units fit too, so no surrogate pair is cut.
    QString name = QcNameText::clean(text, QcItemData::maxNameLength);
    for (qsizetype points = QcItemData::maxNameLength - 1; name.size() > QcItemData::maxNameLength; --points) {
        name = QcNameText::fit(name, points).trimmed();
    }
    return name;
}

} // namespace QcAnvil
