#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/content/definitions/QcVegetationDefinition.h"

#include <QList>
#include <QSet>
#include <QStringList>

class QcContentRegistry;

// One layer of a biome's vegetation (decision E-814) resolved to block ids: a weighted choice
// shared by the world generator and bone meal. Immutable after construction.
class QcVegetationTable
{
public:
    struct Plant
    {
        QcBlockId block = qcAirBlockId;
        int weight = 1;
        // Blocks it may stand on (the block's `ground`); empty: any.
        QSet<QcBlockId> ground;
    };

    // A table and the block names of the layer that were left out: unknown to the content, or
    // solid or liquid blocks, which cannot grow as plants.
    struct Resolved;

    QcVegetationTable() = default;
    [[nodiscard]] static Resolved resolve(const QcContentRegistry &content, const QcVegetationLayer &layer);

    [[nodiscard]] bool isEmpty() const { return m_plants.isEmpty(); }
    [[nodiscard]] const QList<Plant> &plants() const { return m_plants; }
    // The plant a `random` value in [0, 1) chooses by weight; the table must not be empty.
    [[nodiscard]] const Plant &pick(double random) const;

private:
    QList<Plant> m_plants;
    int m_totalWeight = 0;
};

struct QcVegetationTable::Resolved
{
    QcVegetationTable table;
    QStringList unknown;
    QStringList notPlants;
};
