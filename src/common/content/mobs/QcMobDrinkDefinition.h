// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcItemDefinition.h"

#include <QList>
#include <QString>

/*! \brief A creature that drinks potions when it needs them (the witch, decisions E-623, E-1105),
    from its data (\c {drinks}): every game tick it does not drink, the first of its \c {potions}
    whose need holds, whose chance comes up and none of whose lasting effects it has, begins; it
    holds the potion for \c {seconds} without shooting or walking, then gets what the potion's item
    gives when drunk (QcDrinkDefinition::effects).

    Server side only. */
struct QcMobDrinkDefinition
{
    /*! \brief When a potion is drunk. */
    enum class Need : quint8 {
        Underwater, // Its eyes are under water (\c {"underwater"}).
        Burning, // It burns (\c {"burning"}).
        Hurt, // Its health lies below its most (\c {"hurt"}).
        TargetFar, // Its target is farther away than \c {distance} metres (\c {"targetFar"}).
    };

    /*! \brief One potion it may drink: the drink item \c {item}, its \c {need}, the \c {chance}
        per game tick while the need holds, and for Need::TargetFar the \c {distance}. \qcfields */
    struct Potion
    {
        QString item;
        /*! The id of \c {item}, resolved when the content is finished
            (QcMobTraitsContent::resolve). */
        QcItemId itemId = qcNoItem;
        Need need = Need::Hurt;
        double chance = 1.0;
        double distance = 0.0;

        bool operator==(const Potion &) const = default;
    };

    /*! Most entries of \c {potions}. */
    static constexpr int maxPotions = 8;
    /*! The potions in the order they are tried. */
    QList<Potion> potions;
    /*! Seconds it holds a potion before it takes effect. */
    double seconds = 1.6;
    /*! Played when it begins to drink; empty for none. */
    QString sound;
    /*! Chance (0 to 1) that a creature dying while it drinks drops the potion it holds
        (\c {"dropChance"}, decision E-1105). */
    double dropChance = 0.0;

    bool operator==(const QcMobDrinkDefinition &) const = default;
};
