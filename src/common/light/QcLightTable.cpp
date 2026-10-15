#include "QcLightTable.h"

#include "QcContentRegistry.h"

#include <algorithm>

QcLightTable::QcLightTable(const QcContentRegistry &content)
{
    m_entries.resize(std::size_t(content.stateCount()));
    for (std::size_t id = 0; id < m_entries.size(); ++id) {
        const QcBlockState &state = content.state(QcBlockId(id));
        Entry &entry = m_entries[id];
        entry.opacity = quint8(std::clamp(state.lightOpacity, 0, maxLevel));
        entry.emission = quint8(std::clamp(content.block(QcBlockId(id)).lightLevel, 0, maxLevel));
        entry.blockedFaces = state.lightBlockedFaces;
        entry.skyStop = QcLightPassage::skyStop(entry.opacity, entry.blockedFaces);
        m_hasLightSources = m_hasLightSources || entry.emission > 0;
    }
}
