#include "server/maps/QcServerMaps.h"

#include "common/maps/QcMapData.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/maps/QcMapSurvey.h"

#include <cmath>
#include <utility>

QcServerMaps::QcServerMaps(QcServerState &state, QcServerInventory &inventory)
    : m_state(state)
    , m_inventory(inventory)
{
}

std::optional<quint32> QcServerMaps::mapIdOf(QcItemStack stack) const
{
    const QcContentRegistry &content = m_state.content();
    if (stack.isEmpty() || !content.isValidItem(stack.item) || content.item(stack.item).map != QcMapItem::Filled || stack.damage <= 0) {
        return std::nullopt;
    }
    return quint32(stack.damage);
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
    const std::optional<quint32> id = m_state.maps().create(QcMapData::around(int(std::floor(position.x)), int(std::floor(position.z)), 0));
    if (!id) {
        return true;
    }
    // Creative players keep the empty map, like the items they place.
    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    const QcItemStack map{*filled, 1, int(*id)};
    if (client.inventory.selectedStack().isEmpty()) {
        client.inventory.setSlot(client.inventory.selectedHotbarSlot(), map);
    } else {
        m_inventory.giveOrThrow(client, map);
    }
    m_inventory.send(client);
    return true;
}

bool QcServerMaps::mayMakeMap(QcServerClient &client) const
{
    constexpr quint64 minute = quint64(60) * 20;
    const quint64 now = m_state.clock().tick();
    if (now >= client.input.mapWindowStart + minute) {
        client.input.mapWindowStart = now;
        client.input.mapsMade = 0;
    }
    if (client.input.mapsMade >= maxMapsPerMinute) {
        return false;
    }
    ++client.input.mapsMade;
    return true;
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
    // Maps nobody holds leave memory after a while (decision E-799).
    m_state.maps().tick(m_state.clock().tick());
    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing) {
            continue;
        }
        const std::optional<quint32> id = mapIdOf(client->inventory.selectedStack());
        const quint32 before = std::exchange(client->input.heldMap, id.value_or(0));
        if (!id || !m_state.maps().find(*id)) {
            continue;
        }
        if (!client->dead) {
            reveal(*client, *id);
        }
        // A map just taken into the hand goes out whole: the client may have dropped it from its
        // cache (QcClientMaps::maxMaps) since it last had it.
        if (before != *id) {
            m_state.maps().resend(*id, client->id);
        }
        sendTo(*client, *id);
    }
}

void QcServerMaps::forget(const QcServerClient &client)
{
    m_state.maps().forget(client.id);
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

void QcServerMaps::sendTo(QcServerClient &client, quint32 id)
{
    QcMapStore &maps = m_state.maps();
    const bool first = !maps.watches(id, client.id);
    maps.watch(id, client.id);
    const QRect area = maps.takePending(id, client.id, pixelBudget);
    if (!first && area.isEmpty() && m_state.clock().tick() % markerInterval != 0) {
        return;
    }
    const QcMapData &map = *maps.find(id);
    QcMsg::MapUpdate update{id, quint8(map.scale()), map.centre(), area, map.pixelsIn(area), markersFor(client, id, map)};
    client.connection->send(update);
}

bool QcServerMaps::carries(const QcServerClient &client, quint32 id) const
{
    return std::ranges::any_of(client.inventory.allSlots(), [this, id](QcItemStack stack) { return mapIdOf(stack) == id; });
}

QList<QcMapMarker> QcServerMaps::markersFor(const QcServerClient &receiver, quint32 id, const QcMapData &map) const
{
    QList<QcMapMarker> markers;
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
        const auto clamped = [](double value) { return qint8(std::clamp(int(std::floor(value)), -128, 127)); };
        const int rotation = int(std::lround(double(client->yaw) / 22.5)) % 16;
        markers.append({clamped(offset.x()), clamped(offset.y()), quint8((rotation + 16) % 16), onMap, own});
    }
    return markers;
}
