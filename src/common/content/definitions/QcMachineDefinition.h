#pragma once

#include "common/content/definitions/QcCookingDefinitions.h"

#include <QString>
#include <QStringList>

// Block capability of a machine (furnace, smoker, blast furnace): an input, a fuel and an output slot
// that work through the smelting recipes of `category` while fuel burns; `kinds` narrows them to
// recipes of those kinds (decision E-770).
struct QcMachineDefinition
{
    // Recipe category this machine processes; empty for blocks that are not machines.
    QString category;
    // Work done per second of burning, relative to a recipe's `seconds`; fuel burns at the same rate.
    double speed = 1.0;
    // Sound heard now and then while fuel burns; empty for a silent machine.
    QString sound;
    // Recipe kinds this machine takes (`QcSmeltingRecipe::kind`); empty: every recipe of its category.
    QStringList kinds = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisations warning-free

    bool isMachine() const { return !category.isEmpty(); }
    // Whether a recipe of this kind (empty: no kind) works in this machine.
    bool accepts(const QString &kind) const { return qcAcceptsRecipeKind(kinds, kind); }
};
