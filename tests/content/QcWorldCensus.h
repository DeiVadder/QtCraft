// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "content/QcProgressionSolver.h"

#include "common/core/QcBlockPos.h"

#include <QHash>
#include <QList>
#include <QString>

#include <optional>

class QcContentRegistry;
class QcWorldGenerator;
struct QcDimensionDefinition;

// What the generator of a dimension really produces around its spawn (decision E-1097): the blocks
// of a sample of real chunks, the chests, creatures and villagers there, as progression nodes with
// how many were found. What the sample misses counts as not there, so a rare structure never
// carries a goal by luck.
struct QcWorldCensus
{
    // How much of the land the census looks at: every `step`th chunk within `radius` chunks around
    // `centre`, by default the spawn.
    struct Area
    {
        int radius = 16;
        int step = 3;
        std::optional<QcBlockPos> centre = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisations warning-free
    };

    QString dimension;
    // Node -> how many there are in the sample.
    QHash<QString, qint64> found;

    // The census of `generator`, which generates the dimension `definition` of `content`.
    [[nodiscard]] static QcWorldCensus take(const QcWorldGenerator &generator, const QcDimensionDefinition &definition, const QcContentRegistry &content,
                                            const Area &area);
    // The rules of standing in the dimension: everything found there is there.
    [[nodiscard]] QList<QcProgressionRule> rules() const;
};
