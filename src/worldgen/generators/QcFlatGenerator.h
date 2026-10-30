// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"
#include "worldgen/generators/QcWorldGenerator.h"

#include <memory>

/*! \brief World generator of the flat world type (decision E-223): the same layers everywhere, from
    a preset like "bedrock,59*stone,3*dirt,grass_block" (bottom up, \c {n*} repeats a layer, names
    without a namespace are the base game's).

    No caves, ores, features or structures; the spawn is on top at the origin. The world's rules
    (worldgen/flat.json) are set up once. Deterministic, seed ignored. */
class QcFlatGenerator : public QcWorldGenerator
{
public:
    /*! Returns the block per height from y 0 up that \a preset names, with block names looked up
        in \a content; an error for unknown blocks, bad counts, no layers or too many layers. */
    [[nodiscard]] static QcResult<QList<QcBlockId>> parseLayers(const QString &preset, const QcContentRegistry &content);

    /*! Builds the layers from \a preset with the blocks of \a content. An invalid \a preset falls
        back to the content's default with a warning; empty takes the default. */
    QcFlatGenerator(std::shared_ptr<const QcContentRegistry> content, const QString &preset);

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override;
    QcBlockPos spawnPoint() const override { return {0, static_cast<int>(m_layers.size()), 0}; }
    bool isNaturalGround(QcBlockId block) const override { return !m_layers.isEmpty() && block == m_layers.constLast(); }
    QcWorldSetup setup() const override;

    /*! Returns the block per height from y 0 up. */
    const QList<QcBlockId> &layers() const { return m_layers; }

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QList<QcBlockId> m_layers;
};
