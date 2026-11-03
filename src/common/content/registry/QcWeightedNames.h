// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief A block picked by weight among others (decision E-975): the qualified \c {block} name
    (key \c {"block"}) and its relative \c {weight} (key \c {"weight"}, at least 1). Village crops
    and vegetation plants use it. \qcfields */
struct QcWeightedBlockName
{
    QString block;
    int weight = 1;
};

/*! \brief A structure picked by weight among others (decision E-975): the qualified
    \c {structure} name (key \c {"structure"}) and its relative \c {weight} (key \c {"weight"}, at
    least 1). Village blueprints and coastal towers use it. \qcfields */
struct QcWeightedStructureName
{
    QString structure;
    int weight = 1;
};
