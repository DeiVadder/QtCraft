#pragma once

#include "QcBlockAccess.h"
#include "QcContentRegistry.h"

#include <QHash>

// Sparse block world for tests: everything not set explicitly is air (or unloaded outside `loadedRadius`).
class QcTestWorld : public QcBlockAccess
{
public:
    explicit QcTestWorld(const QcContentRegistry &content)
        : m_content(content)
    {
    }

    void set(QcBlockPos pos, QcBlockId block) { m_blocks.insert(pos, block); }
    void fillBox(QcBlockPos from, QcBlockPos to, QcBlockId block)
    {
        for (int x = from.x; x <= to.x; ++x) {
            for (int y = from.y; y <= to.y; ++y) {
                for (int z = from.z; z <= to.z; ++z) {
                    set({x, y, z}, block);
                }
            }
        }
    }
    // Blocks farther than this (Chebyshev, horizontally) count as unloaded.
    int loadedRadius = 1000;

    std::optional<QcBlockId> blockAt(QcBlockPos pos) const override
    {
        if (std::abs(pos.x) > loadedRadius || std::abs(pos.z) > loadedRadius) {
            return std::nullopt;
        }
        return m_blocks.value(pos, qcAirBlockId);
    }
    const QcContentRegistry &content() const override { return m_content; }

private:
    const QcContentRegistry &m_content;
    QHash<QcBlockPos, QcBlockId> m_blocks;
};

// Registry with a solid "test:stone" and a liquid "test:water" block.
inline QcContentRegistry qcMakeTestContent()
{
    QcContentRegistry content;
    QcBlockDefinition stone;
    stone.name = QStringLiteral("test:stone");
    stone.faceTextures.fill(QStringLiteral("test:stone"));
    if (!content.registerBlock(stone)) {
        qFatal("cannot register test:stone");
    }

    QcBlockDefinition water;
    water.name = QStringLiteral("test:water");
    water.renderLayer = QcRenderLayer::Translucent;
    water.solid = false;
    water.opaque = false;
    water.liquid = true;
    water.replaceable = true;
    water.lightOpacity = 2;
    water.faceTextures.fill(QStringLiteral("test:water"));
    if (!content.registerBlock(water)) {
        qFatal("cannot register test:water");
    }

    return content;
}
