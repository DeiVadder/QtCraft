// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcMobDefinition.h"

#include <QStringList>

#include <optional>

class QcContentRegistry;

// Checks the traits of creatures (decision E-460) when the content is finished.
namespace QcMobTraitsContent {

// One trait of a creature checked against the content: what is left of it and the warnings.
template<typename Trait>
struct Part
{
    std::optional<Trait> value;
    QStringList warnings;
};

// The traits of a creature checked against the content, and the warnings in the order of the traits.
struct Resolved
{
    std::optional<QcTameDefinition> tameable;
    std::optional<QcCrumbleDefinition> crumbles;
    std::optional<QcDrinkHealDefinition> drinks;
    std::optional<QcCarryDefinition> carries;
    std::optional<QcFamilyDefinition> family;
    QStringList warnings;
};

// Resolves the blocks a creature carries (QcCarryDefinition::blockIds): a name without a block, or a
// block without an item of its own (it could not drop when the creature dies), with a block entity
// (storage, machine, sapling, circuit part) or of two cells (door, bed) is left out with a warning;
// a creature left without any carries nothing.
// Also resolves the kind a tamed one becomes (QcTameDefinition::becomesType; a name that is no
// tameable creature is ignored with a warning) and warns of unknown taming, healing and dye items,
// and leaves out crumbling and drinking that name unknown blocks or items, and a family whose child
// is no creature or has a family of its own (decision E-839).
[[nodiscard]] Resolved resolve(const QcContentRegistry &content, const QcMobDefinition &mob);
// The taming part of resolve (decision E-480).
[[nodiscard]] Part<QcTameDefinition> resolveTame(const QcContentRegistry &content, const QcMobDefinition &mob);
// Warns of structure creatures with an owner (QcStructureDefinition::Mob::owner) of a kind that is not
// tameable: they are placed wild.
[[nodiscard]] QStringList checkStructurePets(const QcContentRegistry &content);

} // namespace QcMobTraitsContent
