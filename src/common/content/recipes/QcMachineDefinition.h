// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/recipes/QcCookingDefinitions.h"

#include <QString>
#include <QStringList>

#include <optional>

/*! \brief What makes a machine a brewing stand (decision E-1063, \c {"brewing"} in \c {"machine"}):
    an ingredient slot, a fuel slot and three bottle slots instead of the furnace's three, working
    through the brewing recipes (QcPotionCatalog) instead of smelting recipes. Server side only. */
struct QcBrewerDefinition
{
    /*! Slots of a brewing stand: ingredient, fuel and three bottles. */
    static constexpr int slotCount = 5;
    /*! The first bottle slot; the slot numbers before it are those of every machine. */
    static constexpr int firstBottleSlot = 2;

    /*! Qualified name of its fuel item (\c {"fuel"}, required). */
    QString fuel;
    /*! Brews one fuel item lasts (\c {"fuelUses"}, 1 to 1000, default 20). */
    int fuelUses = 20;
    /*! Seconds of a brew (\c {"seconds"}, 0.05 to 3600, default 20). */
    double seconds = 20.0;
    /*! Sound of a finished brew; empty for none. */
    QString finishSound = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisations warning-free
};

/*! \brief Block capability of a machine (furnace, smoker, blast furnace): an input, a fuel and
    an output slot that work through the smelting recipes of \c {category} while fuel burns;
    \c {kinds} narrows them to recipes of those kinds (decision E-770). */
struct QcMachineDefinition
{
    /*! Slots of a furnace-like machine: input, fuel and output. */
    static constexpr int furnaceSlotCount = 3;

    /*! Recipe category this machine processes; empty for blocks that are not machines. */
    QString category;
    /*! Work done per second of burning, relative to a recipe's \c {seconds}; fuel burns at the
        same rate. */
    double speed = 1.0;
    /*! Sound heard now and then while fuel burns; empty for a silent machine. */
    QString sound;
    /*! Recipe kinds this machine takes (\c {QcSmeltingRecipe::kind}); empty: every recipe of its
        category. */
    QStringList kinds = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisations warning-free
    /*! A brewing stand instead of a furnace (decision E-1063); unset for other machines. */
    std::optional<QcBrewerDefinition> brewing = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisations warning-free

    /*! Returns whether the block is a machine (\c {category} is set). */
    bool isMachine() const { return !category.isEmpty(); }
    /*! Returns the number of slots of the machine. */
    int slotCount() const { return brewing ? QcBrewerDefinition::slotCount : furnaceSlotCount; }
    /*! Returns whether a recipe of \a kind (empty: no kind) works in this machine. */
    bool accepts(const QString &kind) const { return qcAcceptsRecipeKind(kinds, kind); }
};
