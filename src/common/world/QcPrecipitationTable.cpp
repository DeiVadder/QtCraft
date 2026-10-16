#include "QcPrecipitationTable.h"

#include "QcChunk.h"
#include "QcContentRegistry.h"
#include "QcLightPassage.h"

#include <algorithm>

QcPrecipitationTable::QcPrecipitationTable(const QcContentRegistry &content)
{
    m_stops.resize(std::size_t(content.stateCount()));
    for (std::size_t id = 0; id < m_stops.size(); ++id) {
        const QcBlockState &state = content.state(QcBlockId(id));
        const QcBlockDefinition &block = content.block(QcBlockId(id));
        const int opacity = std::clamp(state.lightOpacity, 0, 15);
        const bool skyStop = QcLightPassage::skyStop(opacity, state.lightBlockedFaces) != QcLightPassage::SkyStop::None;
        const bool collides = block.solid && !state.collision.isEmpty();
        m_stops[id] = skyStop || collides || block.liquid ? 1 : 0;
    }
}

int QcPrecipitationTable::top(const QcChunk &chunk, int lx, int lz) const
{
    for (int section = QcWorld::sectionCount - 1; section >= 0; --section) {
        if (chunk.section(section).isEmpty()) {
            continue;
        }
        for (int y = (section + 1) * QcWorld::sectionHeight - 1; y >= section * QcWorld::sectionHeight; --y) {
            if (stops(chunk.block(lx, y, lz))) {
                return y;
            }
        }
    }
    return -1;
}
