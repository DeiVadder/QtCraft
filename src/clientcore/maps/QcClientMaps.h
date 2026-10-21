#pragma once

#include "common/maps/QcMapData.h"
#include "common/maps/QcMapMarker.h"
#include "common/protocol/QcMapMessages.h"

#include <QHash>
#include <QList>

// The maps the server sent (decision E-802), part of the world mirror: per map id its picture
// (QcMapData), the markers of its last update and a revision that grows with every update, so a
// drawn image of the map can tell it is out of date. The server sends a map only to players holding
// it; the client keeps at most maxMaps of them and forgets the one updated longest ago.
class QcClientMaps
{
public:
    static constexpr int maxMaps = 256;

    struct Map
    {
        QcMapData data;
        QList<QcMapMarker> markers;
        // Unique over all maps of the cache: a map forgotten and sent again gets a new one.
        quint64 revision = 0;
        // The revision that last changed the pixels (markers move far more often).
        quint64 pictureRevision = 0;
    };

    // Applies an update: the first one of an id, or one with another scale or centre, starts a blank
    // map; the pixels of its area are written, the markers replaced. An update whose scale or centre
    // is out of range is ignored; pixels that do not fit the area are left out (logged).
    void apply(const QcMsg::MapUpdate &update);

    // The map `id`, or null while the client has none.
    const Map *find(quint32 id) const;
    qsizetype count() const { return m_maps.size(); }

private:
    // Forgets the map updated longest ago while there are more than maxMaps.
    void evict();

    QHash<quint32, Map> m_maps;
    quint64 m_revision = 0;
};
