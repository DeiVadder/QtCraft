// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>

class QcContentRegistry;

/*! \brief The registry function behind potions (decision E-1061): for every kind of potion
    (QcPotionCatalog) and each of its forms, it registers the item, for thrown forms the
    projectile, the texture and the display name, like QcPartGenerator does for building parts.

    The names follow from the potion: \c {<name>_potion}, \c {<name>_splash_potion} and
    \c {<name>_lingering_potion}, so the items of 0.1.0 (\c {healing_potion},
    \c {harming_splash_potion}, \c {poison_splash_potion}) are generated under their old ids; a
    potion may name its drink item (\c {water_bottle}). Whatever a pack defined under such a name
    stays and only counts as that form of the potion. */
namespace QcPotionGenerator {

/*! Generates the items of all potions into \a registry; returns warnings about what names
    nothing. */
QStringList generate(QcContentRegistry &registry);

} // namespace QcPotionGenerator
