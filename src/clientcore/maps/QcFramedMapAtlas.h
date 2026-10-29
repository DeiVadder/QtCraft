// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/maps/QcClientMaps.h"

#include <QImage>
#include <QList>
#include <QRectF>
#include <QRgb>

#include <memory>

// The pictures of the maps in item frames (decision E-809), one texture for all of them: a grid of
// columns x columns tiles of QcMapData::size pixels, one per map id drawn this frame, so every
// framed map is drawn with the same texture and a wall of them costs one draw call. A map's tile is
// drawn again only when its picture or its frame markers change, never per frame or per frame
// entity. Tile 0 is blank paper: a map the client has not received yet, and every map beyond the
// nearest tileCount - 1 of the frame, shows it. Each frame starts with the maps in view nearest
// first (beginFrame): they keep their tiles, a map new among them takes the tile unused longest, so
// tiles never pass from map to map within a frame. Lives in the GUI thread; the renderer uploads the
// tiles whose version it has not seen (tiles()).
class QcFramedMapAtlas
{
public:
    static constexpr int columns = 8;
    static constexpr int tileCount = columns * columns;
    // The texture's side in pixels.
    static constexpr int side = columns * QcMapData::size;

    // One tile as the renderer uploads it: the picture (RGBA8888, QcMapData::size squared, null for a
    // tile never drawn) and a version that changes with every new picture.
    struct Tile
    {
        QImage image;
        quint64 version = 0;
    };

    QcFramedMapAtlas();

    // The colour of each block definition (QcMapColors::ofAtlas): every tile is drawn again.
    void setColours(QList<QRgb> colours);
    // A new frame showing the maps `nearestFirst` (by the distance of their nearest frame to the
    // camera; an id may come more than once): the nearest tileCount - 1 of them that `maps` has a
    // picture of keep their tiles or get the tiles unused longest, the others show paper this frame.
    void beginFrame(const QList<quint32> &nearestFirst, const QcClientMaps &maps);
    // The texture coordinates of map `id` (inset half a pixel, so neighbouring tiles never bleed in),
    // drawing its tile from `maps` when the map changed since; the paper tile when it got no tile in
    // beginFrame.
    [[nodiscard]] QRectF place(quint32 id, const QcClientMaps &maps);
    // Every tile; a new list only when a tile was drawn, so handing it to the renderer each frame
    // copies nothing.
    [[nodiscard]] std::shared_ptr<const QList<Tile>> tiles() const { return m_tiles; }

    // The texture coordinates of tile `index`, inset half a pixel.
    [[nodiscard]] static QRectF uvOf(int index);
    // The picture of `map` on its tile: the pixels in their colours, blank ones paper, and the frame
    // markers (QcMapMarker::frame) as small green pointers; players are not shown on framed maps.
    [[nodiscard]] static QImage draw(const QcClientMaps::Map &map, const QList<QRgb> &colours);

private:
    struct Slot
    {
        quint32 id = 0;
        // What the tile shows: the map's picture revision and its frame markers.
        quint64 pictureRevision = 0;
        QList<QcMapMarker> markers;
        quint64 lastFrame = 0;
        bool drawn = false;
    };

    // The tile of map `id` in this frame; -1 when it has none.
    [[nodiscard]] int slotOf(quint32 id) const;
    // Gives map `id` the tile not in use this frame that was unused longest.
    void claim(quint32 id);
    void setTile(int index, QImage image);

    QList<QRgb> m_colours;
    // Slot 0 is the paper tile and never holds a map.
    QList<Slot> m_slots = QList<Slot>(tileCount);
    std::shared_ptr<QList<Tile>> m_tiles;
    quint64 m_frame = 1;
    quint64 m_version = 0;
};
