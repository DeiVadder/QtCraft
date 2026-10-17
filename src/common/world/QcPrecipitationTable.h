#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

#include <vector>

class QcChunk;
class QcContentRegistry;

// Which block states keep rain and snow out of the cells below them (decision E-395): those that
// keep sky light coming straight down out (QcLightPassage::skyStop: opaque blocks, leaves, covered
// tops), solid blocks with a collision shape (glass, fences, slabs) and liquids. Torches, flowers,
// tall grass and other blocks without collision let it through. The server and the client read the
// same table, so rain falls in the same places for both. Immutable after construction.
class QcPrecipitationTable
{
public:
    // Without content every id reads as air.
    QcPrecipitationTable() = default;
    explicit QcPrecipitationTable(const QcContentRegistry &content);

    // Unknown ids read as air.
    bool stops(QcBlockId id) const { return id < m_stops.size() && m_stops[id] != 0; }
    // Highest cell of the column (lx, lz) of `chunk` that stops precipitation; -1 for none.
    int top(const QcChunk &chunk, int lx, int lz) const;

private:
    std::vector<quint8> m_stops;
};
