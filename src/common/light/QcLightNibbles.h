// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcWorldConstants.h"

#include <QtGlobal>

#include <array>

/*! \brief Light levels (0..15) of the cells of one section, two per byte (2 KiB per section).

    Cells are indexed like QcChunkSection::index (x fastest, then z, then y). */
class QcLightNibbles
{
public:
    /*! The cells of a section. */
    static constexpr int cellCount = QcWorld::blocksPerSection;

    explicit QcLightNibbles(int level = 0) { fill(level); }

    /*! Returns the level of the cell \a index. */
    int level(int index) const
    {
        const quint8 pair = m_data[static_cast<std::size_t>(index >> 1)];
        return (index & 1) != 0 ? pair >> 4 : pair & 0x0F;
    }
    /*! Sets the cell \a index to \a level (0..15). */
    void setLevel(int index, int level)
    {
        quint8 &pair = m_data[static_cast<std::size_t>(index >> 1)];
        pair = (index & 1) != 0 ? static_cast<quint8>((pair & 0x0F) | (level << 4)) : static_cast<quint8>((pair & 0xF0) | level);
    }
    /*! Sets every cell to \a level (0..15). */
    void fill(int level) { m_data.fill(static_cast<quint8>(level * 0x11)); }
    /*! Returns the level of every cell when all cells have the same one, else -1. */
    int uniformLevel() const
    {
        const quint8 first = m_data[0];
        if ((first >> 4) != (first & 0x0F)) {
            return -1;
        }
        for (const quint8 pair : m_data) {
            if (pair != first) {
                return -1;
            }
        }
        return first & 0x0F;
    }
    bool operator==(const QcLightNibbles &other) const = default;

private:
    std::array<quint8, cellCount / 2> m_data{};
};
