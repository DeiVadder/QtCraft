#pragma once

#include "QcContentRegistry.h"
#include "QcResult.h"
#include "QcWorldGenerator.h"

#include <memory>

// World generator of the flat world type (decision E-223): the same layers everywhere, from a
// preset like "bedrock,59*stone,3*dirt,grass_block" (bottom up, `n*` repeats a layer, names without
// a namespace are the base game's). No caves, ores, features or structures; the spawn is on top at
// the origin. The world's rules (worldgen/flat.json) are set up once. Deterministic, seed ignored.
class QcFlatGenerator : public QcWorldGenerator
{
public:
    // Block per height from y 0 up; an error for unknown blocks, bad counts or too many layers.
    static QcResult<QList<QcBlockId>> parseLayers(const QString &preset, const QcContentRegistry &content);

    // An invalid `preset` falls back to the content's default with a warning; empty takes the default.
    QcFlatGenerator(std::shared_ptr<const QcContentRegistry> content, const QString &preset, QStringList *warnings = nullptr);

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override;
    QcBlockPos spawnPoint() const override { return {0, int(m_layers.size()), 0}; }
    bool isNaturalGround(QcBlockId block) const override { return !m_layers.isEmpty() && block == m_layers.constLast(); }
    QcWorldSetup setup() const override;

    const QList<QcBlockId> &layers() const { return m_layers; }

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QList<QcBlockId> m_layers;
};
