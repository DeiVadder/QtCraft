// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/world/QcVegetationTable.h"

#include <QHash>
#include <QList>
#include <QRandomGenerator>
#include <QString>

#include <optional>

class QcContentRegistry;
class QcServerWorld;

/*! \brief What bone meal grows on a block that \c {growsVegetation} (a grass block, decision
    E-814), as in the reference: \c {attempts} tries each walk from the cell above it up to attempt
    / 16 random steps (one block sideways, sometimes one up or down) over cells whose block below
    also grows vegetation and that are not solid; a try ending in air plants a flower of the biome
    there with one chance in \c {flowerOdds} (if the biome has flowers), otherwise its grass.

    The biome is the generator's at the end of the walk, the terrain's \c {vegetationBiome} where
    the generator names none. */
class QcVegetationSpread
{
public:
    /*! The tries per use, the tries per step a walk may take (try number / attemptsPerStep
        steps) and the odds of a flower, as described above. \qcshared */
    static constexpr int attempts = 128;
    static constexpr int attemptsPerStep = 16;
    static constexpr int flowerOdds = 8;

    /*! \brief A plant to set: the grass or flower \c {block} at \c {pos}. \qcfields */
    struct Planting
    {
        QcBlockPos pos;
        QcBlockId block = qcAirBlockId;
    };

    explicit QcVegetationSpread(const QcContentRegistry &content);

    /*! Returns the plants bone meal on \a target of \a world grows, drawn from \a random, each
        cell at most once; the world is not changed. */
    [[nodiscard]] QList<Planting> spread(const QcServerWorld &world, QcBlockPos target, QRandomGenerator &random) const;

private:
    struct Tables
    {
        QcVegetationTable grass;
        QcVegetationTable flowers;
    };

    // The tables of the biome at column (x, z); null where there are none.
    [[nodiscard]] const Tables *tablesAt(const QcServerWorld &world, int x, int z) const;
    // Where a walk of `steps` steps from `start` ends, std::nullopt where it leaves the vegetation.
    [[nodiscard]] std::optional<QcBlockPos> walk(const QcServerWorld &world, QcBlockPos start, int steps, QRandomGenerator &random) const;
    // Whether a walk may stand at `pos`: not inside a solid block, on a block that grows vegetation.
    [[nodiscard]] bool isWalkable(const QcServerWorld &world, QcBlockPos pos) const;

    const QcContentRegistry &m_content;
    QHash<QString, Tables> m_biomes;
    QString m_fallbackBiome;
};
