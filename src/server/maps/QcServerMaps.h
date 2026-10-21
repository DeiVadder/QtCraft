#pragma once

#include "common/maps/QcMapMarker.h"

#include <QList>

#include <optional>

class QcServerInventory;
class QcServerState;
class QcMapData;
struct QcServerClient;
struct QcItemStack;

// What players do with maps in the server (decisions E-800 to E-804): using an empty map makes a
// filled map of the land around its user, holding a filled map reveals the land around its holder,
// and the holder receives what changed of it, at most pixelBudget pixels per tick, with the
// players who carry the map as markers.
class QcServerMaps
{
public:
    // Pixels a player receives per tick at most (a whole new map in four ticks).
    static constexpr int pixelBudget = 4096;
    // Ticks between two marker updates of a held map when no pixels go out.
    static constexpr int markerInterval = 4;
    // New maps one player may make per minute of ticks (creative players keep their empty map, so
    // only this bounds the files a player can add to the world).
    static constexpr int maxMapsPerMinute = 20;

    QcServerMaps(QcServerState &state, QcServerInventory &inventory);

    // The use button with an empty map in the hand: the map becomes a filled map (scale 0) of the
    // land around the player (one map of a stack; the filled map goes into the inventory or drops),
    // at most maxMapsPerMinute per player. False when the held item is no empty map.
    bool useHeld(QcServerClient &client);
    // Reveals the held maps and sends what changed (QcMsg::MapUpdate); unused maps leave memory
    // (QcMapStore::tick).
    void tick();
    // The player left.
    void forget(const QcServerClient &client);

    // The map id of a filled map stack, std::nullopt for anything else.
    [[nodiscard]] std::optional<quint32> mapIdOf(QcItemStack stack) const;
    // A new map one scale larger around the same land as map `id` (zooming out, decision E-804);
    // std::nullopt for an unknown map, one at the largest scale, or when the ids are used up.
    [[nodiscard]] std::optional<quint32> zoomOut(quint32 id);

private:
    // Counts a new map of `client`; false when it made maxMapsPerMinute in the current minute.
    bool mayMakeMap(QcServerClient &client) const;
    void reveal(const QcServerClient &holder, quint32 id);
    void sendTo(QcServerClient &client, quint32 id);
    [[nodiscard]] QList<QcMapMarker> markersFor(const QcServerClient &receiver, quint32 id, const QcMapData &map) const;
    bool carries(const QcServerClient &client, quint32 id) const;

    QcServerState &m_state;
    QcServerInventory &m_inventory;
};
