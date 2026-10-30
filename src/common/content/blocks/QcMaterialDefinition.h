// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

/*! \brief A material that building parts are made of: a base block and its kind.

    Exclusion entries (\c {excluded}) list base blocks that never get parts, whatever material
    definition names them. */
struct QcMaterialDefinition
{
    /*! Qualified material name (\c {"name"}). */
    QString name;
    /*! Base block the parts are made of (\c {"block"}); empty for an exclusion entry. */
    QString block;
    /*! Material kind that selects the part families (\c {"kind"}, e.g. "wood", "stone"). */
    QString kind;
    /*! Part families (by name) left out for this material (\c {"exclude"}). */
    QStringList exclude;
    /*! Translation key of the material's name, which fills %1 of the families' display names
        (\c {"translation"}; "material.<namespace>.<path>" unless set). */
    QString translationKey;
    /*! Whether this is an exclusion entry (\c {"excluded": true}) rather than a material. */
    bool excluded = false;
    /*! Base blocks of an exclusion entry (\c {"blocks"}). */
    QStringList excludedBlocks;
};
