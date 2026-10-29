#include "server/advancements/QcAdvancementMatch.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcInventory.h"

namespace QcAdvancementMatch {

bool matchesName(const QcContentRegistry &content, const QString &pattern, const QString &name)
{
    if (pattern.isEmpty()) {
        return true;
    }
    if (!pattern.startsWith(u'#')) {
        return pattern == name;
    }
    const std::optional<QcItemId> id = content.itemId(name);
    return id && content.item(*id).tags.contains(pattern.mid(1));
}

bool matches(const QcContentRegistry &content, const QcAdvancementConditions &conditions, const QcAdvancementEvent &event)
{
    const auto same = [](const QString &wanted, const QString &actual) { return wanted.isEmpty() || wanted == actual; };
    return matchesName(content, conditions.item, event.item) && matchesName(content, conditions.block, event.block)
           && same(conditions.entity, event.entity) && same(conditions.vehicle, event.vehicle) && same(conditions.effect, event.effect)
           && event.level >= conditions.level;
}

bool holds(const QcContentRegistry &content, const QcAdvancementConditions &conditions, const QcInventory &inventory)
{
    int count = 0;
    for (int slot = 0; slot < QcInventory::slotCount && count < conditions.count; ++slot) {
        const QcItemStack &stack = inventory.slot(slot);
        if (!stack.isEmpty() && matchesName(content, conditions.item, content.item(stack.item).name)) {
            count += stack.count;
        }
    }
    return count >= conditions.count;
}

bool matchesPlace(const QcAdvancementConditions &conditions, const Place &place)
{
    const bool high = !conditions.minY || place.y >= *conditions.minY;
    const bool low = !conditions.maxY || place.y <= *conditions.maxY;
    const bool biome = conditions.biome.isEmpty() || conditions.biome == place.biome;
    const bool structure = conditions.structure.isEmpty() || place.inVillage;
    return high && low && biome && structure;
}

} // namespace QcAdvancementMatch
