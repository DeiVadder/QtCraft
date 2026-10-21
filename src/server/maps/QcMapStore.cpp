#include "server/maps/QcMapStore.h"

#include <algorithm>

void QcMapStore::open(Loader loader, quint32 nextId)
{
    m_loader = std::move(loader);
    m_nextId = std::clamp(nextId, 1u, maxId + 1);
    m_maps.clear();
    m_missing.clear();
    m_unsaved.clear();
    m_saving.clear();
}

std::optional<quint32> QcMapStore::create(QcMapData map)
{
    if (m_nextId > maxId) {
        return std::nullopt;
    }
    const quint32 id = m_nextId++;
    m_maps.insert(id, Entry{std::move(map), {}, m_now});
    m_missing.remove(id);
    m_unsaved.insert(id);
    return id;
}

QcMapStore::Entry *QcMapStore::entry(quint32 id)
{
    if (const auto found = m_maps.find(id); found != m_maps.end()) {
        found->lastUse = m_now;
        return &found.value();
    }
    if (id == 0 || id >= m_nextId || m_missing.contains(id) || !m_loader) {
        return nullptr;
    }
    std::optional<QcMapData> stored = m_loader(id);
    if (!stored) {
        m_missing.insert(id);
        return nullptr;
    }
    return &m_maps.insert(id, Entry{std::move(*stored), {}, m_now}).value();
}

const QcMapData *QcMapStore::find(quint32 id)
{
    const Entry *found = entry(id);
    return found ? &found->data : nullptr;
}

const QcMapData *QcMapStore::loaded(quint32 id) const
{
    const auto found = m_maps.constFind(id);
    return found != m_maps.cend() ? &found->data : nullptr;
}

bool QcMapStore::set(quint32 id, QPoint pixel, quint16 value)
{
    Entry *found = entry(id);
    if (!found || !QcMapData::bounds().contains(pixel) || !found->data.set(pixel, value)) {
        return false;
    }
    m_unsaved.insert(id);
    for (QcMapPending &waiting : found->pending) {
        waiting.add(pixel);
    }
    return true;
}

void QcMapStore::watch(quint32 id, quint32 client)
{
    Entry *found = entry(id);
    if (found && !found->pending.contains(client)) {
        found->pending.insert(client, QcMapPending::whole());
    }
}

void QcMapStore::resend(quint32 id, quint32 client)
{
    if (Entry *found = entry(id)) {
        found->pending.insert(client, QcMapPending::whole());
    }
}

bool QcMapStore::watches(quint32 id, quint32 client) const
{
    const auto found = m_maps.constFind(id);
    return found != m_maps.cend() && found->pending.contains(client);
}

QRect QcMapStore::takePending(quint32 id, quint32 client, int budget)
{
    Entry *found = entry(id);
    if (!found || !found->pending.contains(client)) {
        return {};
    }
    return found->pending[client].take(budget);
}

void QcMapStore::forget(quint32 client)
{
    for (Entry &map : m_maps) {
        map.pending.remove(client);
    }
}

QList<quint32> QcMapStore::takeUnsaved()
{
    QList<quint32> ids = m_unsaved.values();
    std::ranges::sort(ids);
    m_unsaved.clear();
    for (const quint32 id : std::as_const(ids)) {
        ++m_saving[id];
    }
    return ids;
}

void QcMapStore::finishSave(const QList<quint32> &ids, bool written)
{
    for (const quint32 id : ids) {
        if (const auto saving = m_saving.find(id); saving != m_saving.end() && --saving.value() <= 0) {
            m_saving.erase(saving);
        }
        if (!written && m_maps.contains(id)) {
            m_unsaved.insert(id);
        }
    }
}

void QcMapStore::tick(quint64 now)
{
    m_now = now;
    // Without a save to read them from again, maps stay (a world without a directory).
    if (!m_loader || now % evictionInterval != 0) {
        return;
    }
    m_maps.removeIf([this](const QHash<quint32, Entry>::iterator found) {
        const quint32 id = found.key();
        return found->lastUse + idleTicks <= m_now && !m_unsaved.contains(id) && !m_saving.contains(id);
    });
}
