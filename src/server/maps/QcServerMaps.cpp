#include "server/maps/QcServerMaps.h"

#include "common/maps/QcMapData.h"
#include "server/core/QcServerState.h"
#include "server/entities/QcEntities.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/maps/QcMapSurvey.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

// A position in half pixels from the map's middle, kept on the map's edge.
qint8 clampedHalfPixel(double value)
{
    return static_cast<qint8>(std::clamp(static_cast<int>(std::floor(value)), -128, 127));
}

} // namespace

QcServerMaps::QcServerMaps(QcServerState &state, QcServerInventory &inventory)
    : m_state(state)
    , m_inventory(inventory)
{
}

std::optional<quint32> QcServerMaps::mapIdOf(const QcItemStack &stack) const
{
    const QcContentRegistry &content = m_state.content();
    if (stack.isEmpty() || !content.isValidItem(stack.item) || content.item(stack.item).map != QcMapItem::Filled || stack.mapId() == 0) {
        return std::nullopt;
    }
    return stack.mapId();
}

bool QcServerMaps::useHeld(QcServerClient &client)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    if (!held || held->map != QcMapItem::Empty) {
        return false;
    }
    const std::optional<QcItemId> filled = m_state.content().itemId(held->filledMap);
    if (client.dead || !filled || m_state.content().item(*filled).map != QcMapItem::Filled) {
        return true;
    }

    if (!mayMakeMap(client)) {
        return true;
    }
    const QcVec3 &position = client.motion.position;
    const std::optional<quint32> id = m_state.maps().create(QcMapData::around(static_cast<int>(std::floor(position.x)),
        static_cast<int>(std::floor(position.z)), 0));
    if (!id) {
        return true;
    }
    // Creative players keep the empty map, like the items they place.
    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    const QcItemStack map = QcItemStack{*filled, 1, 0}.withMapId(*id);
    if (client.inventory.selectedStack().isEmpty()) {
        client.inventory.setSlot(client.inventory.selectedHotbarSlot(), map);
    } else {
        m_inventory.giveOrThrow(client, map);
    }
    m_inventory.send(client);
    return true;
}

bool QcServerMaps::mayMakeMap(const QcServerClient &client) const
{
    return !client.rules().infiniteItems || m_state.maps().countMade(client.name, m_state.clock().tick(), maxMapsPerMinute);
}

std::optional<quint32> QcServerMaps::zoomOut(quint32 id)
{
    const QcMapData *map = m_state.maps().find(id);
    if (!map || map->scale() >= QcMapData::maxScale) {
        return std::nullopt;
    }
    return m_state.maps().create(QcMapData::around(map->centre().x(), map->centre().y(), map->scale() + 1));
}

void QcServerMaps::tick()
{
    const quint64 now = m_state.clock().tick();
    const bool evicting = now % QcMapStore::evictionInterval == 0;
    const bool anyPlaying = std::ranges::any_of(m_state.clients(), [](const auto &client) { return client->state == QcServerClient::State::Playing; });
    // The frame table is only needed for players and for the eviction check.
    const FramedMaps framed = anyPlaying || evicting ? framedMaps() : FramedMaps();
    // Maps in frames count as used (decision E-807); the others leave memory after a while
    // (decision E-799).
    if (evicting) {
        for (auto map = framed.cbegin(); map != framed.cend(); ++map) {
            m_state.maps().touch(map.key(), now);
        }
    }
    m_state.maps().tick(now);

    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing) {
            continue;
        }
        const QList<quint32> seen = mapsSeenBy(*client, framed);
        watchOnly(*client, seen);
        const std::optional<quint32> held = mapIdOf(client->inventory.selectedStack());
        if (held && !client->dead && m_state.maps().find(*held)) {
            reveal(*client, *held);
        }
        int budget = pixelBudget;
        for (const quint32 id : seen) {
            budget -= sendTo(*client, id, budget, framed);
        }
    }
}

void QcServerMaps::forget(const QcServerClient &client)
{
    m_state.maps().forget(client.id);
}

QcServerMaps::FramedMaps QcServerMaps::framedMaps() const
{
    FramedMaps framed;
    for (const QcHangingEntity &hanging : m_state.entities().hangings().all()) {
        if (hanging.hanging.type != QcHangingType::ItemFrame) {
            continue;
        }
        if (const std::optional<quint32> id = mapIdOf(hanging.held)) {
            framed[*id].append(hanging.hanging);
        }
    }
    return framed;
}

QList<quint32> QcServerMaps::mapsSeenBy(const QcServerClient &client, const FramedMaps &framed)
{
    QList<quint32> seen;
    const std::optional<quint32> held = mapIdOf(client.inventory.selectedStack());
    if (held && m_state.maps().find(*held)) {
        seen.append(*held);
    }

    // The nearest frame of each map within range; maps watched already count watchSlack blocks
    // nearer, so a player at the edge of the range or of the maxWatched nearest does not make them
    // come and go.
    const QList<quint32> watched = m_state.maps().watched(client.id);
    QList<std::pair<double, quint32>> near;
    const QcVec3 &eye = client.motion.position;
    for (auto map = framed.cbegin(); map != framed.cend(); ++map) {
        double nearest = std::numeric_limits<double>::max();
        for (const QcHanging &hanging : map.value()) {
            const QcVec3 offset = hanging.centre() - eye;
            nearest = std::min(nearest, offset.dot(offset));
        }
        const double distance = std::sqrt(nearest) - (watched.contains(map.key()) ? watchSlack : 0.0);
        if (distance < frameWatchRange && map.key() != held) {
            near.append({distance, map.key()});
        }
    }
    std::ranges::sort(near);
    for (const auto &[distance, id] : std::as_const(near)) {
        if (seen.size() >= maxWatched) {
            break;
        }
        if (m_state.maps().find(id)) {
            seen.append(id);
        }
    }
    return seen;
}

void QcServerMaps::watchOnly(const QcServerClient &client, const QList<quint32> &seen)
{
    QcMapStore &maps = m_state.maps();
    const QList<quint32> before = maps.watched(client.id);
    for (const quint32 id : before) {
        if (!seen.contains(id)) {
            maps.unwatch(id, client.id);
        }
    }
    for (const quint32 id : seen) {
        if (!before.contains(id)) {
            maps.resend(id, client.id);
        }
    }
    maps.setWatched(client.id, seen);
}

void QcServerMaps::reveal(const QcServerClient &holder, quint32 id)
{
    QcMapStore &maps = m_state.maps();
    const QcMapData &map = *maps.find(id);
    const QPointF where(holder.motion.position.x, holder.motion.position.z);
    const QList<QPoint> due = QcMapSurvey::pixelsDue(map, where, m_state.clock().tick());
    const QList<QcMapSurvey::Surveyed> shown = QcMapSurvey::survey(m_state.world(), map, due);
    for (const QcMapSurvey::Surveyed &pixel : shown) {
        maps.set(id, pixel.pixel, pixel.value);
    }
}

int QcServerMaps::sendTo(QcServerClient &client, quint32 id, int budget, const FramedMaps &framed)
{
    QcMapStore &maps = m_state.maps();
    const bool first = !maps.watches(id, client.id);
    maps.watch(id, client.id);
    const QRect area = budget > 0 ? maps.takePending(id, client.id, budget) : QRect();
    const quint64 now = m_state.clock().tick();
    const bool held = mapIdOf(client.inventory.selectedStack()) == id;
    const bool markersDue = held ? now % markerInterval == 0 : (now + id) % framedMarkerInterval == 0;
    if (!first && area.isEmpty() && !markersDue) {
        return 0;
    }
    const QcMapData &map = *maps.find(id);
    QcMsg::MapUpdate update{id, static_cast<quint8>(map.scale()), map.centre(), area, map.pixelsIn(area), markersFor(client, id, map, framed)};
    client.connection->send(update);
    return area.width() * area.height();
}

bool QcServerMaps::carries(const QcServerClient &client, quint32 id) const
{
    return std::ranges::any_of(client.inventory.allSlots(), [this, id](const QcItemStack &stack) { return mapIdOf(stack) == id; });
}

QList<QcMapMarker> QcServerMaps::markersFor(const QcServerClient &receiver, quint32 id, const QcMapData &map, const FramedMaps &framed) const
{
    QList<QcMapMarker> markers;
    const QList<QcHanging> frames = framed.value(id);
    for (const QcHanging &hanging : frames) {
        if (markers.size() >= QcMapMarker::maxCount / 2) {
            break;
        }
        // A frame off the map would sit on its edge and point towards where it hangs.
        const QcMapMarker marker = frameMarker(map, hanging);
        if (marker.onMap) {
            markers.append(marker);
        }
    }
    // Players show only on maps in the hand; a framed map shows its frames (as in the reference).
    if (mapIdOf(receiver.inventory.selectedStack()) != id) {
        return markers;
    }
    for (const auto &client : m_state.clients()) {
        if (markers.size() >= QcMapMarker::maxCount) {
            break;
        }
        const bool own = client.get() == &receiver;
        if (client->state != QcServerClient::State::Playing || client->dead || (!own && !carries(*client, id))) {
            continue;
        }
        const QPointF offset = map.offsetOf(client->motion.position.x, client->motion.position.z);
        const bool onMap = offset.x() >= -128.0 && offset.x() < 128.0 && offset.y() >= -128.0 && offset.y() < 128.0;
        const int rotation = static_cast<int>(std::lround(static_cast<double>(client->yaw) / 22.5)) % 16;
        markers.append({clampedHalfPixel(offset.x()), clampedHalfPixel(offset.y()), static_cast<quint8>((rotation + 16) % 16), onMap, own});
    }
    return markers;
}

QcMapMarker QcServerMaps::frameMarker(const QcMapData &map, const QcHanging &hanging)
{
    const QcVec3 centre = hanging.centre();
    const QPointF offset = map.offsetOf(centre.x, centre.z);
    const bool onMap = offset.x() >= -128.0 && offset.x() < 128.0 && offset.y() >= -128.0 && offset.y() < 128.0;
    int rotation = 0;
    switch (hanging.face) {
    case QcBlockFace::North:
        rotation = 0;
        break;
    case QcBlockFace::East:
        rotation = 4;
        break;
    case QcBlockFace::South:
        rotation = 8;
        break;
    case QcBlockFace::West:
        rotation = 12;
        break;
    // On a floor or ceiling the picture turns a quarter per step (QcHangingMesher::mapQuarterTurns),
    // clockwise as seen from the front; at step 0 its top points north on a floor and south on a
    // ceiling (QcHanging::rightOf), and seen from below clockwise is anticlockwise from above.
    case QcBlockFace::Up:
        rotation = hanging.rotation % 4 * 4;
        break;
    case QcBlockFace::Down:
        rotation = (8 - hanging.rotation % 4 * 4 + 16) % 16;
        break;
    }
    QcMapMarker marker{clampedHalfPixel(offset.x()), clampedHalfPixel(offset.y()), static_cast<quint8>(rotation), onMap, false};
    marker.frame = true;
    return marker;
}
