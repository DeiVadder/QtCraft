#pragma once

#include "common/gameplay/QcEquipment.h"

#include <QList>

#include <optional>

class QcContentRegistry;

// Armour rules (decision E-234), pure so that the server's damage handling is testable on its own:
// every armour point takes 4 % off a hit up to 20 points (80 %); each hit wears every worn piece
// with durability by a quarter of the damage (at least 1), and a piece breaks at its durability.
namespace QcArmour {

inline constexpr double reductionPerPoint = 0.04;
inline constexpr int maxEffectivePoints = 20;
// Damage per point of wear.
inline constexpr double damagePerWear = 4.0;

// The armour of an item; null for items that are no armour or unknown.
const QcArmourDefinition *of(const QcContentRegistry &content, QcItemId item);
// Whether the stack may be worn in `slot`: one armour item made for that slot.
bool fits(const QcContentRegistry &content, const QcItemStack &stack, QcEquipmentSlot slot);
// The item an empty `slot` shows as a pale placeholder: the first armour item for it in content
// order (mods included); nothing when the content has none.
[[nodiscard]] std::optional<QcItemId> placeholder(const QcContentRegistry &content, QcEquipmentSlot slot);
// The item the HUD shows for armour points: the chest piece with the most points (the first of
// equals in content order); nothing without chest armour.
[[nodiscard]] std::optional<QcItemId> pointsIcon(const QcContentRegistry &content);
// Sum of the armour points of the worn items.
int points(const QcContentRegistry &content, const QcEquipment &equipment);
// What is left of a hit of `damage` against `points` armour points.
double reducedDamage(double damage, int points);
// Durability lost by every worn piece in a hit of `damage` (before the reduction).
int wearOf(double damage);

} // namespace QcArmour
