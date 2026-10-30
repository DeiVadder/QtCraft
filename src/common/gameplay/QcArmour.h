// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcEquipment.h"

#include <QList>

#include <optional>

class QcContentRegistry;

/*! \brief Armour rules (decision E-234), pure so that the server's damage handling is testable
    on its own: every armour point takes 4 % off a hit up to 20 points (80 %); each hit wears
    every worn piece with durability by a quarter of the damage (at least 1), and a piece breaks
    at its durability. */
namespace QcArmour {

/*! The share of a hit each armour point takes off. */
inline constexpr double reductionPerPoint = 0.04;
/*! Armour points beyond this count take nothing more off. */
inline constexpr int maxEffectivePoints = 20;
/*! Damage per point of wear. */
inline constexpr double damagePerWear = 4.0;

/*! Returns the armour of \a item in \a content; null for items that are no armour or unknown. */
const QcArmourDefinition *of(const QcContentRegistry &content, QcItemId item);
/*! Returns whether \a stack may be worn in \a slot: one armour item of \a content made for that
    slot. */
bool fits(const QcContentRegistry &content, const QcItemStack &stack, QcEquipmentSlot slot);
/*! Returns the item an empty \a slot shows as a pale placeholder: the first armour item for it
    in the order of \a content (mods included); nothing when the content has none. */
[[nodiscard]] std::optional<QcItemId> placeholder(const QcContentRegistry &content, QcEquipmentSlot slot);
/*! Returns the item the HUD shows for armour points: the chest piece of \a content with the most
    points (the first of equals in content order); nothing without chest armour. */
[[nodiscard]] std::optional<QcItemId> pointsIcon(const QcContentRegistry &content);
/*! Returns the sum of the armour points of the items worn in \a equipment, as \a content defines
    them. */
int points(const QcContentRegistry &content, const QcEquipment &equipment);
/*! Returns what is left of a hit of \a damage against \a points armour points. */
double reducedDamage(double damage, int points);
/*! Returns the durability lost by every worn piece in a hit of \a damage (before the
    reduction). */
int wearOf(double damage);

} // namespace QcArmour
