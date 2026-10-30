// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief Turns one input item into a result inside a machine of the same category (furnace:
    "smelting"); the stonecutter's recipes (decision E-784) have the category "stonecutting" and
    their own list. */
struct QcSmeltingRecipe
{
    /*! Category of a recipe that names none: the furnace's. */
    static constexpr auto defaultCategory = "smelting";
    /*! Category of every stonecutting recipe; a smelting recipe may not name it. */
    static constexpr auto stonecuttingCategory = "stonecutting";
    /*! Lower limit of \c {seconds}. */
    static constexpr double minSeconds = 0.05;
    /*! Upper limit of \c {seconds}. */
    static constexpr double maxSeconds = 3600.0;

    /*! Qualified name (\c {"name"}, required). */
    QString name;
    /*! Category of the machines that use it (\c {"category"}, default defaultCategory). */
    QString category = QString::fromLatin1(defaultCategory);
    /*! Qualified name of the item it takes (\c {"input"}, required). */
    QString inputItem;
    /*! Qualified name of the item it makes (\c {"item"} in \c {"result"}, required). */
    QString resultItem;
    /*! Items it makes per input item (\c {"count"} in \c {"result"}, 1 to 64). */
    int resultCount = 1;
    /*! Burning time per item at machine speed 1. */
    double seconds = 10.0;
    /*! Experience points per item smelted, gathered in the machine until a player takes the
        output (decision E-398). Server side only. */
    double experience = 0.0;
    /*! Upper limit of \c {experience}. */
    static constexpr double maxExperience = 100.0;
    /*! What is smelted ("food", "metal"; empty: anything else). Machines with \c {kinds} take
        only recipes of those kinds: the smoker food, the blast furnace metal (decision E-770). */
    QString kind = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisations warning-free
};
