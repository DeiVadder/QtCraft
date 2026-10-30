// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/world/QcLightPassage.h"

#include <QtGlobal>

#include <vector>

class QcContentRegistry;

/*! \brief How every block state treats light, indexed by runtime block id (decision E-389): the
    opacity (light lost entering the cell, 15 stops it), the block light it gives off and the faces
    light cannot cross (QcLightPassage).

    Built once from the content on both sides; the light engine of the server and of the client
    read the same values. Immutable after construction. */
class QcLightTable
{
public:
    /*! The highest light level. */
    static constexpr int maxLevel = 15;

    /*! \brief How one block state treats light. */
    struct Entry
    {
        /*! Light lost entering the cell; 15 stops it. */
        quint8 opacity = 0;
        /*! The block light it gives off. */
        quint8 emission = 0;
        /*! The faces light cannot cross, one bit per QcBlockFace (QcLightPassage). */
        quint8 blockedFaces = 0;
        /*! What the cell does to sky light coming straight down. */
        QcLightPassage::SkyStop skyStop = QcLightPassage::SkyStop::None;

        /*! Returns whether light passes differently after a change from \a other to this entry. */
        bool passageDiffers(Entry other) const
        {
            return opacity != other.opacity || blockedFaces != other.blockedFaces;
        }
    };

    /*! Without content every id reads as air. */
    QcLightTable() = default;
    explicit QcLightTable(const QcContentRegistry &content);

    /*! Returns the entry of the block state \a id; unknown ids read as air. */
    const Entry &entry(QcBlockId id) const { return id < m_entries.size() ? m_entries[id] : m_air; }
    /*! Returns whether any block state gives off light. */
    bool hasLightSources() const { return m_hasLightSources; }

private:
    std::vector<Entry> m_entries;
    Entry m_air;
    bool m_hasLightSources = false;
};
