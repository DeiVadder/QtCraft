#include "clientcore/maps/QcClientMaps.h"

#include "clientcore/debug/QcClientLogging.h"

void QcClientMaps::apply(const QcMsg::MapUpdate &update)
{
    Map *map = nullptr;
    const auto found = m_maps.find(update.id);
    if (found != m_maps.end() && found->data.scale() == update.scale && found->data.centre() == update.centre) {
        map = &*found;
    } else {
        std::optional<QcMapData> blank = QcMapData::fromParts(update.scale, update.centre, QList<quint16>(QcMapData::pixelCount, 0));
        if (!blank) {
            qCWarning(lcSession) << "Ignoring the update of map" << update.id << "with scale" << update.scale << "and centre" << update.centre;
            return;
        }
        map = &m_maps.insert(update.id, Map{std::move(*blank), {}, 0, 0}).value();
    }

    map->revision = ++m_revision;
    if (map->pictureRevision == 0) {
        map->pictureRevision = map->revision;
    }
    // The decoder checked area and pixels already; a mismatch only drops the pixels.
    if (!update.area.isEmpty()) {
        if (map->data.setPixels(update.area, update.pixels)) {
            map->pictureRevision = map->revision;
        } else {
            qCWarning(lcSession) << "Ignoring the pixels of map" << update.id << "that do not fit" << update.area;
        }
    }
    map->markers = update.markers.mid(0, QcMapMarker::maxCount);
    evict();
}

const QcClientMaps::Map *QcClientMaps::find(quint32 id) const
{
    const auto found = m_maps.constFind(id);
    return found == m_maps.cend() ? nullptr : &*found;
}

void QcClientMaps::evict()
{
    while (m_maps.size() > maxMaps) {
        auto oldest = m_maps.cbegin();
        for (auto it = m_maps.cbegin(); it != m_maps.cend(); ++it) {
            if (it->revision < oldest->revision) {
                oldest = it;
            }
        }
        m_maps.erase(oldest);
    }
}
