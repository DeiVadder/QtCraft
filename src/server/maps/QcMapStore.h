// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/maps/QcMapData.h"
#include "server/maps/QcMapPending.h"

#include <QHash>
#include <QList>
#include <QRect>
#include <QSet>
#include <QString>

#include <algorithm>
#include <functional>
#include <optional>

/*! \brief The maps of a world (decision E-799) by map id: kept in memory while used, read from the
    save when first needed (the loader), written with the next save only when changed since, and
    dropped from memory when nobody used them for idleTicks and they are written (tick).

    It also keeps, per player that holds a map or stands near a frame showing it, the rectangle
    of pixels that player has not received yet (decisions E-802, E-807), so a revealed pixel goes
    out once to everyone who sees the map. */
class QcMapStore
{
public:
    /*! Reads the stored map \c {id}; std::nullopt when there is none or it is damaged. */
    using Loader = std::function<std::optional<QcMapData>(quint32 id)>;
    /*! Map ids are handed out from 1 up to this; a world with more maps makes no new ones. */
    static constexpr quint32 maxId = 1u << 30;
    /*! A map nobody looked at, changed or held for this many ticks (five minutes) leaves memory. */
    static constexpr quint64 idleTicks = quint64{5} * 60 * 20;
    /*! Ticks between two looks for idle maps. */
    static constexpr quint64 evictionInterval = quint64{60} * 20;

    /*! Sets \a loader and the next free id \a nextId of an opened world. */
    void open(Loader loader, quint32 nextId);
    /*! Returns the id the next new map gets (saved with the world). */
    quint32 nextId() const { return m_nextId; }
    /*! Ids below \a next are used (the next id saved with the world): new maps get larger ones. */
    void reserveIdsBelow(quint32 next) { m_nextId = std::clamp(std::max(m_nextId, next), 1u, maxId + 1); }

    /*! Keeps \a map under a new id. Returns the id; std::nullopt when the ids are used up. */
    [[nodiscard]] std::optional<quint32> create(QcMapData map);
    /*! Returns the map \a id, read from the save when it is not in memory; nullptr for an
        unknown id. */
    const QcMapData *find(quint32 id);
    /*! Returns the map \a id when it is in memory (never reads the save); nullptr otherwise. */
    const QcMapData *loaded(quint32 id) const;
    /*! Sets \a pixel of map \a id in memory to \a value. Returns whether it changed. */
    bool set(quint32 id, QPoint pixel, quint16 value);

    /*! \a client holds map \a id now: a map it never had comes out whole (every pixel shown so
        far). */
    void watch(quint32 id, quint32 client);
    /*! \a client gets map \a id whole again with the next takePending (it took the map into the
        hand). */
    void resend(quint32 id, quint32 client);
    /*! \a client no longer holds map \a id nor stands near a frame showing it: nothing is kept
        for it (a later watch sends the map whole again). Never reads the save. */
    void unwatch(quint32 id, quint32 client);
    /*! Map \a id is shown somewhere (an item frame in a loaded chunk, decision E-807): it counts
        as used at tick \a now, if it is in memory. Never reads the save. */
    void touch(quint32 id, quint64 now);
    /*! Returns whether \a client got map \a id before (since it joined). */
    bool watches(quint32 id, quint32 client) const;
    /*! Returns at most \a budget pixels of what \a client has not received of map \a id
        (QcMapPending::take); the rest stays for later; an empty rectangle when nothing waits. */
    [[nodiscard]] QRect takePending(quint32 id, quint32 client, int budget);
    /*! The player \a client left: nothing is kept for it. */
    void forget(quint32 client);
    /*! Returns the maps \a client watched in the last tick (QcServerMaps). */
    QList<quint32> watched(quint32 client) const { return m_watched.value(client); }
    /*! Sets \a ids as the maps \a client watched in this tick. */
    void setWatched(quint32 client, const QList<quint32> &ids);

    /*! Counts a new map of \a player at tick \a now (decision E-810). Returns \c false when it made
        \a perMinute maps in its current minute already. Kept by player name, so reconnecting
        starts no new minute. */
    bool countMade(const QString &player, quint64 now, int perMinute);

    /*! Returns the ids of the maps changed since the last call (for a save); they stay in memory
        until finishSave. */
    [[nodiscard]] QList<quint32> takeUnsaved();
    /*! The save of the maps \a ids (from takeUnsaved) is done; when it failed (\a written false),
        they are written again with the next one. */
    void finishSave(const QList<quint32> &ids, bool written);
    /*! Tick \a now of the world: every evictionInterval ticks the maps unused for idleTicks
        leave memory, unless they wait for a save or the world has no save to read them from
        again. */
    void tick(quint64 now);
    /*! Returns the number of maps in memory. */
    qsizetype loadedCount() const { return m_maps.size(); }

private:
    struct Entry
    {
        QcMapData data;
        // What each player that had the map has not received yet.
        QHash<quint32, QcMapPending> pending;
        // The tick the map was last looked at or changed.
        quint64 lastUse = 0;
    };

    Entry *entry(quint32 id);

    Loader m_loader;
    quint32 m_nextId = 1;
    QHash<quint32, Entry> m_maps;
    // Ids looked up and not stored: not read again.
    QSet<quint32> m_missing;
    QSet<quint32> m_unsaved;
    // Maps taken for saves that are not done yet, with the number of those saves.
    QHash<quint32, int> m_saving;
    quint64 m_now = 0;
    // The maps each player watched in the last tick, by player id.
    QHash<quint32, QList<quint32>> m_watched;
    // The tick each map-making player's minute began and the maps made in it, by player name.
    QHash<QString, std::pair<quint64, int>> m_made;
};
