#include "clientcore/maps/QcFramedMapAtlas.h"

#include "clientcore/maps/QcMapColors.h"
#include "clientcore/settings/QcUiPalette.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

// The paper under a map's blank pixels, as in the hand (MapInHand.qml).
QRgb paper()
{
    return QcUiPalette::color(QcUiPalette::Role::MapPaper).rgb();
}

QList<QcMapMarker> frameMarkers(const QList<QcMapMarker> &markers)
{
    QList<QcMapMarker> frames;
    for (const QcMapMarker &marker : markers) {
        if (marker.frame) {
            frames.append(marker);
        }
    }
    return frames;
}

// A pointer of a frame marker: a wide body with a narrow tip ahead, north before rotation.
void drawMarker(QPainter &painter, QcMapMarker marker)
{
    const double halfPixels = QcMapData::size / 2.0;
    painter.save();
    painter.translate(halfPixels + marker.x / 2.0 + 0.5, halfPixels + marker.z / 2.0 + 0.5);
    painter.rotate(marker.onMap ? marker.rotation * 22.5 : 0.0);
    painter.setPen(QPen(QcUiPalette::color(QcUiPalette::Role::MapPaperEdge), 0.6));
    painter.setBrush(QcUiPalette::color(QcUiPalette::Role::MapMarkerFrame));
    if (marker.onMap) {
        QPainterPath arrow;
        arrow.moveTo(0.0, -3.5);
        arrow.lineTo(2.5, 3.0);
        arrow.lineTo(0.0, 1.5);
        arrow.lineTo(-2.5, 3.0);
        arrow.closeSubpath();
        painter.drawPath(arrow);
    } else {
        painter.drawEllipse(QPointF(0.0, 0.0), 1.5, 1.5);
    }
    painter.restore();
}

} // namespace

QcFramedMapAtlas::QcFramedMapAtlas()
    : m_tiles(std::make_shared<QList<Tile>>(tileCount))
{
    QImage blank(QcMapData::size, QcMapData::size, QImage::Format_RGBA8888);
    blank.fill(QColor::fromRgb(paper()));
    setTile(0, std::move(blank));
}

void QcFramedMapAtlas::setColours(QList<QRgb> colours)
{
    m_colours = std::move(colours);
    for (Slot &slot : m_slots) {
        slot.drawn = false;
    }
}

QRectF QcFramedMapAtlas::uvOf(int index)
{
    const double inset = 0.5 / side;
    const double tile = double(QcMapData::size) / side;
    const int column = index % columns;
    const int row = index / columns;
    const QPointF corner(column * tile, row * tile);
    return {corner.x() + inset, corner.y() + inset, tile - 2.0 * inset, tile - 2.0 * inset};
}

void QcFramedMapAtlas::beginFrame(const QList<quint32> &nearestFirst, const QcClientMaps &maps)
{
    ++m_frame;
    // The maps that get a tile: the nearest ones with a picture, each once.
    QList<quint32> shown;
    for (const quint32 id : nearestFirst) {
        if (shown.size() >= tileCount - 1) {
            break;
        }
        if (!shown.contains(id) && maps.find(id)) {
            shown.append(id);
        }
    }

    // Tiles stay with their maps first, so a map new in the frame never takes a tile shown now.
    QList<quint32> newcomers;
    for (const quint32 id : std::as_const(shown)) {
        const auto own = std::ranges::find(m_slots, id, &Slot::id);
        if (own != m_slots.end() && own != m_slots.begin()) {
            own->lastFrame = m_frame;
        } else {
            newcomers.append(id);
        }
    }
    for (const quint32 id : std::as_const(newcomers)) {
        claim(id);
    }
}

void QcFramedMapAtlas::claim(quint32 id)
{
    int oldest = -1;
    for (int index = 1; index < tileCount; ++index) {
        const Slot &slot = m_slots.at(index);
        if (slot.lastFrame != m_frame && (oldest < 0 || slot.lastFrame < m_slots.at(oldest).lastFrame)) {
            oldest = index;
        }
    }
    if (oldest > 0) {
        Slot fresh;
        fresh.id = id;
        fresh.lastFrame = m_frame;
        m_slots[oldest] = fresh;
    }
}

int QcFramedMapAtlas::slotOf(quint32 id) const
{
    for (int index = 1; index < tileCount; ++index) {
        const Slot &slot = m_slots.at(index);
        if (slot.id == id && slot.lastFrame == m_frame) {
            return index;
        }
    }
    return -1;
}

QRectF QcFramedMapAtlas::place(quint32 id, const QcClientMaps &maps)
{
    const QcClientMaps::Map *map = maps.find(id);
    const int index = map ? slotOf(id) : -1;
    if (index < 0) {
        return uvOf(0);
    }

    Slot &slot = m_slots[index];
    QList<QcMapMarker> markers = frameMarkers(map->markers);
    if (!slot.drawn || slot.pictureRevision != map->pictureRevision || slot.markers != markers) {
        setTile(index, draw(*map, m_colours));
        slot.pictureRevision = map->pictureRevision;
        slot.markers = std::move(markers);
        slot.drawn = true;
    }
    return uvOf(index);
}

void QcFramedMapAtlas::setTile(int index, QImage image)
{
    // A new list for the renderer, which may still hold the old one; the images are shared.
    auto tiles = std::make_shared<QList<Tile>>(*m_tiles);
    (*tiles)[index] = {std::move(image), ++m_version};
    m_tiles = std::move(tiles);
}

QImage QcFramedMapAtlas::draw(const QcClientMaps::Map &map, const QList<QRgb> &colours)
{
    QImage image(QcMapData::size, QcMapData::size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor::fromRgb(paper()));
    QPainter painter(&image);
    painter.drawImage(0, 0, QcMapColors::render(map.data, colours));
    painter.setRenderHint(QPainter::Antialiasing);
    for (const QcMapMarker &marker : map.markers) {
        if (marker.frame) {
            drawMarker(painter, marker);
        }
    }
    painter.end();
    return image.convertToFormat(QImage::Format_RGBA8888);
}
