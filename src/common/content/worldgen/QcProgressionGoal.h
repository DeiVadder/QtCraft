// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QtGlobal>

/*! \brief Something a player must be able to reach in every world (progression/<name>.json of a
    pack, decision E-1095): an item, food, fire, a portal or a dimension.

    Client and server load goals with the content, but nothing in the game reads them beyond the
    check of their names when the content is finished; test_progression_reachability checks them
    for every start dimension from the content and what the generators produce, and names the
    missing step. Mods add goals or replace one by name. */
struct QcProgressionGoal
{
    /*! The most items an item goal may ask for. */
    static constexpr int maxCount = 9999;

    /*! What the goal asks for. */
    enum class Kind : quint8 {
        /*! \c {count} of the item \c {target} (\c {"item"}). */
        Item,
        /*! Any item that can be eaten (\c {"food": true}). */
        Food,
        /*! A way to light fire (\c {"fire": true}). */
        Fire,
        /*! Lighting the portal \c {target} (\c {"portal"}). */
        Portal,
        /*! Standing in the dimension \c {target} (\c {"dimension"}). */
        Dimension,
    };

    /*! Qualified name (\c {"name"}, required). */
    QString name;
    /*! What it asks for; the file names exactly one of the keys of Kind. */
    Kind kind = Kind::Item;
    /*! The qualified name of the item, portal or dimension; empty for food and fire. */
    QString target;
    /*! How many of the item (\c {"count"}, 1 to maxCount, default 1, only with \c {"item"}). */
    int count = 1;
};
