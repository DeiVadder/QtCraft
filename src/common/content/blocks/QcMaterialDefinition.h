// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

// A material that building parts are made of: a base block and its kind. Exclusion entries
// (`excluded`) list base blocks that never get parts, whatever material definition names them.
struct QcMaterialDefinition
{
    QString name;
    QString block;
    QString kind;
    // Part families (by name) left out for this material.
    QStringList exclude;
    QString translationKey;
    bool excluded = false;
    // Base blocks of an exclusion entry.
    QStringList excludedBlocks;
};
