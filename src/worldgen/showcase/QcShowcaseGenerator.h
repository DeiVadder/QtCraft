// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "worldgen/generators/QcWorldGenerator.h"
#include "worldgen/showcase/QcShowcasePlan.h"

#include <memory>

/*! \brief World generator of the showcase world (generator type "showcase", decisions E-220 and
    E-221): flat ground and the structures of a QcShowcasePlan, labels by chunk and the one-time
    setup (noon that stands still, no creatures spawning by themselves, the creatures and contents
    of the areas).

    The seed is ignored: the world depends only on the content. Thread-safe like every generator. */
class QcShowcaseGenerator : public QcWorldGenerator
{
public:
    /*! Plans the showcase world of \a content (QcShowcasePlanner). */
    explicit QcShowcaseGenerator(std::shared_ptr<const QcContentRegistry> content);

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override;
    QcBlockPos spawnPoint() const override { return m_plan.spawn; }
    bool isNaturalGround(QcBlockId block) const override { return block == m_plan.groundColumn.constLast(); }
    QcWorldSetup setup() const override { return m_plan.setup; }
    QList<QcWorldLabel> labelsIn(QcChunkPos pos) const override { return m_plan.labelsByChunk.value(pos); }
    QList<QcBlockPos> pointsOfInterestIn(QcChunkPos pos) const override { return m_plan.pointsOfInterestByChunk.value(pos); }
    QcChunkFurnishing furnishingIn(QcChunkPos pos) const override { return m_plan.furnishingByChunk.value(pos); }
    QcStructureSites structureSitesNear(QcBlockPos from, int reach) const override;

    /*! Returns the row of the village area whose buildings and road reach into the chunk \a pos
        (the rows lie further apart than a chunk). */
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> villageFor(QcChunkPos pos) const override;
    /*! Returns the layout the world is generated from. */
    const QcShowcasePlan &plan() const { return m_plan; }

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QcShowcasePlan m_plan;
};
