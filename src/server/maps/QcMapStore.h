#pragma once

#include "common/maps/QcMapData.h"
#include "server/maps/QcMapPending.h"

#include <QHash>
#include <QList>
#include <QRect>
#include <QSet>

#include <algorithm>
#include <functional>
#include <optional>

// The maps of a world (decision E-799) by map id: kept in memory while used, read from the save
// when first needed (the loader), written with the next save only when changed since, and dropped
// from memory when nobody used them for idleTicks and they are written (tick). It also
// keeps, per player that holds a map, the rectangle of pixels that player has not received yet
// (decision E-802), so a revealed pixel goes out once to everyone who has the map.
class QcMapStore
{
public:
    // Reads the stored map `id`; std::nullopt when there is none or it is damaged.
    using Loader = std::function<std::optional<QcMapData>(quint32 id)>;
    // Map ids are handed out from 1 up to this; a world with more maps makes no new ones.
    static constexpr quint32 maxId = 1u << 30;
    // A map nobody looked at, changed or held for this many ticks (five minutes) leaves memory.
    static constexpr quint64 idleTicks = quint64(5) * 60 * 20;
    // Ticks between two looks for idle maps.
    static constexpr quint64 evictionInterval = quint64(60) * 20;

    // The loader and the next free id of an opened world.
    void open(Loader loader, quint32 nextId);
    quint32 nextId() const { return m_nextId; }
    // Ids below `next` are used (the next id saved with the world): new maps get larger ones.
    void reserveIdsBelow(quint32 next) { m_nextId = std::clamp(std::max(m_nextId, next), 1u, maxId + 1); }

    // Keeps `map` under a new id; std::nullopt when the ids are used up.
    [[nodiscard]] std::optional<quint32> create(QcMapData map);
    // The map `id`, read from the save when it is not in memory; nullptr for an unknown id.
    const QcMapData *find(quint32 id);
    // The map `id` when it is in memory (never reads the save); nullptr otherwise.
    const QcMapData *loaded(quint32 id) const;
    // Sets one pixel of a map in memory; whether it changed.
    bool set(quint32 id, QPoint pixel, quint16 value);

    // `client` holds map `id` now: a map it never had comes out whole (every pixel shown so far).
    void watch(quint32 id, quint32 client);
    // `client` gets map `id` whole again with the next takePending (it took the map into the hand).
    void resend(quint32 id, quint32 client);
    // Whether `client` got map `id` before (since it joined).
    bool watches(quint32 id, quint32 client) const;
    // At most `budget` pixels of what `client` has not received of map `id` (QcMapPending::take);
    // the rest stays for later; an empty rectangle when nothing waits.
    [[nodiscard]] QRect takePending(quint32 id, quint32 client, int budget);
    // The player left: nothing is kept for it.
    void forget(quint32 client);

    // The ids of the maps changed since the last call (for a save); they stay in memory until
    // finishSave.
    [[nodiscard]] QList<quint32> takeUnsaved();
    // The save of the maps `ids` (from takeUnsaved) is done; when it failed, they are written again
    // with the next one.
    void finishSave(const QList<quint32> &ids, bool written);
    // Tick `now` of the world: every evictionInterval ticks the maps unused for idleTicks leave
    // memory, unless they wait for a save or the world has no save to read them from again.
    void tick(quint64 now);
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
};
