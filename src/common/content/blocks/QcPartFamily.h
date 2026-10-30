// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

/*! \brief A family of building parts (stairs, slabs, walls, fences, gates) that QcPartGenerator
    derives from every material of a matching kind: block, item, recipe and names come from this
    template and the material's base block. */
struct QcPartFamily
{
    /*! Qualified family name (\c {"name"}). */
    QString name;
    /*! Material kinds this family applies to ("wood", "stone"). */
    QStringList kinds;
    /*! The part is called "<material>_<suffix>". */
    QString suffix;
    /*! Block fields of the part (shape, states, render, connections, ...); name, textures,
        sound, tool, hardness, drops and fuel come from the base block. */
    QJsonObject block;
    /*! Factor on the base block's hardness for the part (\c {"hardness"}). */
    double hardnessFactor = 1.0;
    /*! Factor on the base block's fuel seconds for the part (\c {"fuel"}). */
    double fuelFactor = 1.0;
    /*! Shaped recipe ("pattern", "key", "count") as in recipe files; a key mapped to "@" stands
        for the base block, "@<suffix>" for the material's part of that suffix ("@slab", decision
        E-755). */
    QJsonObject recipe;
    /*! Further shaped recipes of the part (\c {"recipes"}), each with a "name" (the recipe is
        "<part>_<name>")
        and an optional "result" ("@slab": a vertical slab back into a slab); at most
        maxMoreRecipes. */
    QJsonArray moreRecipes;
    /*! Most entries of \c {moreRecipes}. */
    static constexpr int maxMoreRecipes = 4;
    /*! The stonecutter turns one base block of a material of these kinds into this many parts
        (decision E-784); 0: it does not cut this family. */
    int stonecutterCount = 0;
    /*! Material kinds the stonecutter cuts this family from (\c {"stonecutter"}, \c {"kinds"}). */
    QStringList stonecutterKinds;
    /*! Display name format per language; %1 is the material's name in that language. */
    QList<std::pair<QString, QString>> translations;
};
