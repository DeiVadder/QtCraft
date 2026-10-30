// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"
#include "worldgen/generators/QcWorldGenerator.h"

#include <memory>

/*! \brief World generator of the void world type (decision E-223): air everywhere except a square
    platform around the spawn (worldgen/void.json: block, radius, height; radius 0 leaves it out),
    so that measurements see only what a scenario builds.

    Its rules (noon standing still, no creatures of their own accord by default) are set up once.
    Deterministic, seed ignored. A world stores its platform as generator options,
    "platform=qtcraft:stone,radius=2,y=63", so that changed defaults do not change it. */
class QcVoidGenerator : public QcWorldGenerator
{
public:
    /*! Returns the settings of \a content with those \a options name; an error for unknown keys
        or blocks and values out of range. */
    [[nodiscard]] static QcResult<QcVoidDefinition> parseOptions(const QString &options, const QcContentRegistry &content);
    /*! Returns the options naming the whole platform of \a settings. */
    static QString optionsOf(const QcVoidDefinition &settings);

    /*! Sets up the platform from \a options and the blocks and settings of \a content. Invalid
        \a options fall back to the content's settings with a warning; empty takes them. */
    explicit QcVoidGenerator(std::shared_ptr<const QcContentRegistry> content, const QString &options = {});

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override;
    QcBlockPos spawnPoint() const override { return {0, m_platformY + 1, 0}; }
    bool isNaturalGround(QcBlockId block) const override { return block == m_platform && block != qcAirBlockId; }
    [[nodiscard]] std::optional<QcBlockId> platformBlock() const override
    {
        return m_platform != qcAirBlockId ? std::optional<QcBlockId>(m_platform) : std::nullopt;
    }
    QcWorldSetup setup() const override;

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QcBlockId m_platform = qcAirBlockId;
    int m_radius = 0;
    int m_platformY = 0;
};
