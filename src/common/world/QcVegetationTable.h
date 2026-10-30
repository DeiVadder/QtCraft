// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/worldgen/QcVegetationDefinition.h"

#include <QList>
#include <QSet>
#include <QStringList>

class QcContentRegistry;

/*! \brief One layer of a biome's vegetation (decision E-814) resolved to block ids: a weighted
    choice shared by the world generator and bone meal.

    Immutable after construction. */
class QcVegetationTable
{
public:
    /*! \brief One plant of the layer. */
    struct Plant
    {
        /*! The plant's block. */
        QcBlockId block = qcAirBlockId;
        /*! Its weight in the choice. */
        int weight = 1;
        /*! Blocks it may stand on (the block's \c {ground}); empty: any. */
        QSet<QcBlockId> ground;
    };

    // A table and the block names of the layer that were left out: unknown to the content, or
    // solid or liquid blocks, which cannot grow as plants.
    struct Resolved;

    QcVegetationTable() = default;
    /*! Returns the table of \a layer with the block ids of \a content and the names left out. */
    [[nodiscard]] static Resolved resolve(const QcContentRegistry &content, const QcVegetationLayer &layer);

    /*! Returns whether the table holds no plant. */
    [[nodiscard]] bool isEmpty() const { return m_plants.isEmpty(); }
    /*! Returns the plants in the order of the layer. */
    [[nodiscard]] const QList<Plant> &plants() const { return m_plants; }
    /*! Returns the plant a \a random value in [0, 1) chooses by weight; the table must not be
        empty. */
    [[nodiscard]] const Plant &pick(double random) const;

private:
    QList<Plant> m_plants;
    int m_totalWeight = 0;
};

/*! \brief A table and the block names of the layer that were left out.
    \struct QcVegetationTable::Resolved
    \inmodule qc_common
    \inheaderfile common/world/QcVegetationTable.h */
struct QcVegetationTable::Resolved
{
    /*! The table of the plants that were kept.
        \variable QcVegetationTable::Resolved::table */
    QcVegetationTable table;
    /*! Names the content does not know.
        \variable QcVegetationTable::Resolved::unknown */
    QStringList unknown;
    /*! Names of solid or liquid blocks, which cannot grow as plants.
        \variable QcVegetationTable::Resolved::notPlants */
    QStringList notPlants;
};
