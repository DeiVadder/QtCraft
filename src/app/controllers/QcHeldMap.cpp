#include "app/controllers/QcHeldMap.h"

#include "app/models/QcMapImageProvider.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/maps/QcMapColors.h"
#include "clientcore/session/QcClientSession.h"

#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Markers count half pixels from the middle: 128 of them reach an edge.
constexpr double halfPixelsToEdge = QcMapData::size;
// Marker rotations are sixteenths of a full turn.
constexpr double degreesPerStep = 360.0 / 16.0;

} // namespace

QcHeldMap::QcHeldMap(const QcSessionParts &parts, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
{
}

QString QcHeldMap::source() const
{
    return u"image://maps/%1/%2-%3"_s.arg(m_mapId).arg(m_pictureRevision).arg(m_colourGeneration);
}

void QcHeldMap::useAtlas(const QcContentRegistry &content, const QcTextureAtlas &atlas)
{
    m_colours = QcMapColors::ofAtlas(content, atlas);
    ++m_colourGeneration;
}

void QcHeldMap::update()
{
    const QcClientSession *session = m_parts.session.get();
    const QcContentRegistry *content = session ? session->content().get() : nullptr;
    const QcItemStack stack = session ? session->inventory().slot(session->inventory().selectedHotbarSlot()) : QcItemStack{};
    const bool held = content && content->isValidItem(stack.item) && content->item(stack.item).map == QcMapItem::Filled;
    const quint32 id = held ? quint32(std::max(0, stack.damage)) : 0;
    const QcClientMaps::Map *map = held ? session->worldMirror().maps().find(id) : nullptr;
    const quint64 revision = map ? map->revision : 0;
    const quint64 pictureRevision = map ? map->pictureRevision : 0;
    const bool redraw = map && (pictureRevision != m_pictureRevision || id != m_mapId || m_drawnGeneration != m_colourGeneration);
    if (held == m_held && id == m_mapId && revision == m_revision && !redraw) {
        return;
    }

    if (redraw && m_provider) {
        m_provider->setImage(id, QcMapColors::render(map->data, m_colours));
    }
    // A new revision with the same picture and markers (they are sent every few ticks) changes
    // nothing QML sees.
    QVariantList markers = map ? markerList(map->markers) : QVariantList();
    const int scale = map ? map->data.scale() : 0;
    const bool same = held == m_held && id == m_mapId && !redraw && scale == m_scale && markers == m_markers
                      && (revision > 0) == known();
    m_drawnGeneration = m_colourGeneration;
    m_held = held;
    m_mapId = id;
    m_revision = revision;
    m_pictureRevision = pictureRevision;
    m_scale = scale;
    m_markers = std::move(markers);
    if (!same) {
        emit changed();
    }
}

QVariantList QcHeldMap::markerList(const QList<QcMapMarker> &markers)
{
    QVariantList list;
    list.reserve(markers.size());
    for (const QcMapMarker &marker : markers) {
        list.append(QVariantMap{
            {u"x"_s, double(marker.x) / halfPixelsToEdge},
            {u"z"_s, double(marker.z) / halfPixelsToEdge},
            {u"rotation"_s, double(marker.rotation % 16) * degreesPerStep},
            {u"onMap"_s, marker.onMap},
            {u"own"_s, marker.own},
            {u"frame"_s, marker.frame},
        });
    }
    return list;
}
