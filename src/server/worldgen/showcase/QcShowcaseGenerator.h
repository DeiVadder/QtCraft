#pragma once

#include "QcContentRegistry.h"
#include "QcShowcasePlan.h"
#include "QcWorldGenerator.h"

#include <memory>

// World generator of the showcase world (generator type "showcase", decisions E-220 and E-221): flat
// ground and the structures of a QcShowcasePlan, labels by chunk and the one-time setup (noon that
// stands still, no creatures spawning by themselves, the creatures and contents of the areas). The
// seed is ignored: the world depends only on the content. Thread-safe like every generator.
class QcShowcaseGenerator : public QcWorldGenerator
{
public:
    explicit QcShowcaseGenerator(std::shared_ptr<const QcContentRegistry> content, QStringList *warnings = nullptr);

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override;
    QcBlockPos spawnPoint() const override { return m_plan.spawn; }
    bool isNaturalGround(QcBlockId block) const override { return block == m_plan.groundColumn.constLast(); }
    QcWorldSetup setup() const override { return m_plan.setup; }
    QList<QcWorldLabel> labelsIn(QcChunkPos pos) const override { return m_plan.labelsByChunk.value(pos); }

    const QcShowcasePlan &plan() const { return m_plan; }

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QcShowcasePlan m_plan;
};
