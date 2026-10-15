#pragma once

#include "QcBlockDefinition.h"
#include "QcLightPassage.h"

#include <QtGlobal>

#include <vector>

class QcContentRegistry;

// How every block state treats light, indexed by runtime block id (decision E-389): the opacity
// (light lost entering the cell, 15 stops it), the block light it gives off and the faces light
// cannot cross (QcLightPassage). Built once from the content on both sides; the light engine of the
// server and of the client read the same values. Immutable after construction.
class QcLightTable
{
public:
    static constexpr int maxLevel = 15;

    struct Entry
    {
        quint8 opacity = 0;
        quint8 emission = 0;
        quint8 blockedFaces = 0;
        // What the cell does to sky light coming straight down.
        QcLightPassage::SkyStop skyStop = QcLightPassage::SkyStop::None;

        // Whether light passes differently after a change from `other` to this entry.
        bool passageDiffers(Entry other) const
        {
            return opacity != other.opacity || blockedFaces != other.blockedFaces;
        }
    };

    // Without content every id reads as air.
    QcLightTable() = default;
    explicit QcLightTable(const QcContentRegistry &content);

    // Unknown ids read as air.
    const Entry &entry(QcBlockId id) const { return id < m_entries.size() ? m_entries[id] : m_air; }
    bool hasLightSources() const { return m_hasLightSources; }

private:
    std::vector<Entry> m_entries;
    Entry m_air;
    bool m_hasLightSources = false;
};
