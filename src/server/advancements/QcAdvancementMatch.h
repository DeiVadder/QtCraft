#pragma once

#include "server/advancements/QcAdvancementEvent.h"

class QcContentRegistry;
class QcInventory;

// Whether an event or a player's state meets the conditions of a criterion (decision E-923). Pure,
// so each condition is testable on its own.
namespace QcAdvancementMatch {

// `pattern` is a content name or "#<tag>" of items; `name` an item or block name. An empty pattern
// accepts everything.
[[nodiscard]] bool matchesName(const QcContentRegistry &content, const QString &pattern, const QString &name);
// The conditions of an event trigger (everything but ItemObtained and Location).
[[nodiscard]] bool matches(const QcContentRegistry &content, const QcAdvancementConditions &conditions, const QcAdvancementEvent &event);
// ItemObtained: the inventory holds at least `count` items matching `item`.
[[nodiscard]] bool holds(const QcContentRegistry &content, const QcAdvancementConditions &conditions, const QcInventory &inventory);

// Where a player stands, as far as the location criteria ask (each field filled only when needed).
struct Place
{
    int y = 0;
    QString biome;
    bool inVillage = false;
};
[[nodiscard]] bool matchesPlace(const QcAdvancementConditions &conditions, const Place &place);

} // namespace QcAdvancementMatch
