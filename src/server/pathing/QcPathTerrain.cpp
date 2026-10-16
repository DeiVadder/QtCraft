#include "QcPathTerrain.h"

#include "QcMobDefinition.h"
#include "QcWorldConstants.h"

QcPathRules QcPathRules::of(const QcMobDefinition &definition)
{
    QcPathRules rules;
    rules.height = definition.height;
    return rules;
}

quint8 QcPathDirectCells::cell(QcBlockPos pos) const
{
    // The void below the world is no floor and no place to stand; unloaded chunks are never entered.
    if (pos.y < 0) {
        return QcPathCell::make(QcPathCell::Kind::Unknown);
    }
    const std::optional<QcBlockId> block = m_world.blockAt(pos);
    return block ? m_table.of(*block) : QcPathCell::make(QcPathCell::Kind::Unknown);
}
