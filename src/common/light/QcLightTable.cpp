#include "common/light/QcLightTable.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

QcLightTable::QcLightTable(const QcContentRegistry &content)
{
    m_entries.resize(static_cast<std::size_t>(content.stateCount()));
    for (std::size_t id = 0; id < m_entries.size(); ++id) {
        const QcBlockState &state = content.state(static_cast<QcBlockId>(id));
        Entry &entry = m_entries[id];
        entry.opacity = static_cast<quint8>(std::clamp(state.lightOpacity, 0, maxLevel));
        entry.emission = static_cast<quint8>(std::clamp(content.block(static_cast<QcBlockId>(id)).lightLevel, 0, maxLevel));
        entry.blockedFaces = state.lightBlockedFaces;
        entry.skyStop = QcLightPassage::skyStop(entry.opacity, entry.blockedFaces);
        m_hasLightSources = m_hasLightSources || entry.emission > 0;
    }
}
