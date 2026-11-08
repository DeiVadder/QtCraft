// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief A recipe of the smithing table (a file in \c {smithing/}, decision E-1115): a template,
    a base item and an addition make the result, which keeps everything the base carried
    (damage, enchantments, name), as the reference's smithing transform does.

    All four are qualified item names. \qcfields */
struct QcSmithingRecipe
{
    QString name;
    QString templateItem;
    QString base;
    QString addition;
    QString result;

    bool operator==(const QcSmithingRecipe &) const = default;
};
