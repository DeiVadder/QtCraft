#pragma once

#include "common/gameplay/QcHanging.h"
#include "common/maps/QcMapMarker.h"

#include <QHash>
#include <QList>

#include <optional>

class QcServerInventory;
class QcServerState;
class QcMapData;
struct QcServerClient;
struct QcItemStack;

// What players do with maps in the server (decisions E-800 to E-804, E-807, E-808): using an empty
// map makes a filled map of the land around its user, holding a filled map reveals the land around
// its holder, and the holder and the players near an item frame showing the map receive what
// changed of it, at most pixelBudget pixels per player and tick, with the players who carry the map
// and the frames showing it as markers. Holds no state of its own (the maps, what each player
// watches and the maps players made are in QcMapStore), so the server makes one where it needs it.
class QcServerMaps
{
public:
    // Pixels a player receives per tick at most (a whole new map in four ticks).
    static constexpr int pixelBudget = 4096;
    // Ticks between two marker updates of a held map when no pixels go out.
    static constexpr int markerInterval = 4;
    // Ticks between two marker updates of a map a player only sees in a frame (frames rarely move).
    static constexpr int framedMarkerInterval = 20;
    // New maps one creative player may make per minute of ticks (decision E-810): creative players
    // keep their empty map, so only this bounds the files they can add to the world; in survival
    // every map costs paper, as in the reference, which has no limit.
    static constexpr int maxMapsPerMinute = 20;
    // Item frames within this many blocks of a player send their maps to it (decision E-807).
    static constexpr double frameWatchRange = 64.0;
    // Maps one player watches at most: the held one and the nearest framed ones.
    static constexpr int maxWatched = 64;
    // Blocks a framed map a player watches already counts nearer (decision E-807): it stops only
    // beyond frameWatchRange plus these, and a new map takes its place among the maxWatched only
    // when it is this much nearer.
    static constexpr double watchSlack = 8.0;

    QcServerMaps(QcServerState &state, QcServerInventory &inventory);

    // The use button with an empty map in the hand: the map becomes a filled map (scale 0) of the
    // land around the player (one map of a stack; the filled map goes into the inventory or drops),
    // at most maxMapsPerMinute per player. False when the held item is no empty map.
    bool useHeld(QcServerClient &client);
    // Reveals the held maps and sends what changed (QcMsg::MapUpdate) to their holders and to the
    // players near frames showing them; maps neither used nor framed leave memory (QcMapStore::tick).
    void tick();
    // The player left.
    void forget(const QcServerClient &client);

    // The map id of a filled map stack, std::nullopt for anything else.
    [[nodiscard]] std::optional<quint32> mapIdOf(const QcItemStack &stack) const;
    // A new map one scale larger around the same land as map `id` (zooming out, decision E-804);
    // std::nullopt for an unknown map, one at the largest scale, or when the ids are used up.
    [[nodiscard]] std::optional<quint32> zoomOut(quint32 id);

    // The marker of an item frame `hanging` on `map` (decision E-808): where it hangs, facing
    // away from its wall (a frame on a floor or ceiling turned like its map).
    [[nodiscard]] static QcMapMarker frameMarker(const QcMapData &map, const QcHanging &hanging);

private:
    // The frames in the loaded world that hold a filled map, by map id.
    using FramedMaps = QHash<quint32, QList<QcHanging>>;
    // Counts a new map of a creative `client`; false when it made maxMapsPerMinute in the current
    // minute. Survival players have no limit.
    bool mayMakeMap(const QcServerClient &client) const;
    [[nodiscard]] FramedMaps framedMaps() const;
    // The maps `client` sees now: the held one first, then those of the nearest frames within
    // frameWatchRange (watched ones watchSlack nearer), at most maxWatched.
    [[nodiscard]] QList<quint32> mapsSeenBy(const QcServerClient &client, const FramedMaps &framed);
    // Starts and stops watching: a map seen anew goes out whole (the client may have dropped it from
    // its cache), one no longer seen keeps nothing for `client`.
    void watchOnly(const QcServerClient &client, const QList<quint32> &seen);
    void reveal(const QcServerClient &holder, quint32 id);
    // Sends what `client` has not received of map `id`, at most `budget` pixels; the pixels sent.
    int sendTo(QcServerClient &client, quint32 id, int budget, const FramedMaps &framed);
    [[nodiscard]] QList<QcMapMarker> markersFor(const QcServerClient &receiver, quint32 id, const QcMapData &map, const FramedMaps &framed) const;
    bool carries(const QcServerClient &client, quint32 id) const;

    QcServerState &m_state;
    QcServerInventory &m_inventory;
};
