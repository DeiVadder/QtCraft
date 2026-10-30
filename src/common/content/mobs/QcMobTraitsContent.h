// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcMobDefinition.h"

#include <QStringList>

#include <optional>

class QcContentRegistry;

/*! \brief Checks the traits of creatures (decision E-460) when the content is finished. */
namespace QcMobTraitsContent {

/*! \brief One trait of a creature checked against the content: what is left of it and the
    warnings. */
template<typename Trait>
struct Part
{
    /*! The trait as resolved; empty when the creature has none or it was left out. */
    std::optional<Trait> value;
    /*! What was wrong with it, one line each. */
    QStringList warnings;
};

/*! \brief The traits of a creature checked against the content, and the warnings in the order of
    the traits. */
struct Resolved
{
    /*! Taming, with the kind a tamed one becomes resolved. */
    std::optional<QcTameDefinition> tameable;
    /*! Crumbling; empty if it names an unknown liquid or block. */
    std::optional<QcCrumbleDefinition> crumbles;
    /*! Drinking, with the item's id; empty if the item is unknown. */
    std::optional<QcDrinkHealDefinition> drinks;
    /*! Carrying, with the block ids; empty if no block is left to carry. */
    std::optional<QcCarryDefinition> carries;
    /*! The family, with the child's index; empty if the child does not qualify. */
    std::optional<QcFamilyDefinition> family;
    /*! The warnings of all traits, in the order of the traits above. */
    QStringList warnings;
};

/*! Returns the traits of the creature \a mob checked against \a content, with warnings.

    Resolves the blocks a creature carries (QcCarryDefinition::blockIds): a name without a block,
    or a block without an item of its own (it could not drop when the creature dies), with a
    block entity (storage, machine, sapling, circuit part) or of two cells (door, bed) is left
    out with a warning; a creature left without any carries nothing. Also resolves the kind a
    tamed one becomes (QcTameDefinition::becomesType; a name that is no tameable creature is
    ignored with a warning) and warns of unknown taming, healing and dye items, and leaves out
    crumbling and drinking that name unknown blocks or items, and a family whose child is no
    creature, flies, lives in water or has a family of its own (decision E-839); unknown items
    among the family's pop drops are warned of. */
[[nodiscard]] Resolved resolve(const QcContentRegistry &content, const QcMobDefinition &mob);
/*! Returns the taming part of resolve() for the creature \a mob in \a content (decision
    E-480). */
[[nodiscard]] Part<QcTameDefinition> resolveTame(const QcContentRegistry &content, const QcMobDefinition &mob);
/*! Returns warnings for the structure creatures in \a content with an owner
    (QcStructureDefinition::Mob::owner) of a kind that is not tameable: they are placed wild. */
[[nodiscard]] QStringList checkStructurePets(const QcContentRegistry &content);

} // namespace QcMobTraitsContent
