#include "server/machines/QcCampfires.h"

#include <algorithm>

bool QcCampfires::Entry::isEmpty() const
{
    return std::ranges::all_of(places, [](const Slot &slot) { return slot.stack.isEmpty(); });
}

QcCampfires::QcCampfires(const QcContentRegistry &content)
{
    const auto count = static_cast<std::size_t>(content.stateCount());
    m_slots.resize(count);
    for (std::size_t id = 0; id < count; ++id) {
        m_slots[id] = content.block(static_cast<QcBlockId>(id)).campfire.slotCount;
    }
}

void QcCampfires::noteBlock(QcBlockPos pos, QcBlockId after)
{
    const auto it = m_entries.constFind(pos);
    if (it == m_entries.cend()) {
        return;
    }

    // A campfire with fewer slots keeps the items that fit, the rest spill.
    const int count = slotsOf(after);
    Entry kept = *it;
    Spill spill{pos, {}};
    for (int index = count; index < QcCampfireDefinition::maxSlots; ++index) {
        if (!kept.places[static_cast<std::size_t>(index)].stack.isEmpty()) {
            spill.stacks.append(kept.places[static_cast<std::size_t>(index)].stack);
        }
        kept.places[static_cast<std::size_t>(index)] = {};
    }
    if (!spill.stacks.isEmpty()) {
        m_spills.append(std::move(spill));
    }
    set(kept);
}

void QcCampfires::forgetChunk(QcChunkPos pos)
{
    const QList<QcBlockPos> positions = m_byChunk.take(pos);
    for (const QcBlockPos &at : positions) {
        m_entries.remove(at);
    }
}

QList<QcCampfires::Entry> QcCampfires::savedOf(QcChunkPos pos) const
{
    QList<QcBlockPos> positions = m_byChunk.value(pos);
    std::ranges::sort(positions);
    QList<Entry> entries;
    entries.reserve(positions.size());
    for (const QcBlockPos &at : std::as_const(positions)) {
        entries.append(m_entries.value(at));
    }
    return entries;
}

QList<QcCampfires::Entry> QcCampfires::takeChunk(QcChunkPos pos)
{
    QList<Entry> entries = savedOf(pos);
    forgetChunk(pos);
    return entries;
}

void QcCampfires::restore(const QList<Entry> &entries, const QcChunk &chunk)
{
    for (const Entry &entry : entries) {
        if (entry.pos.chunk() == chunk.pos() && slotsOf(chunk.block(entry.pos.localX(), entry.pos.y, entry.pos.localZ())) > 0) {
            set(entry);
            noteBlock(entry.pos, chunk.block(entry.pos.localX(), entry.pos.y, entry.pos.localZ()));
        }
    }
}

bool QcCampfires::put(QcBlockPos pos, int slotCount, QcItemId item)
{
    Entry entry = m_entries.value(pos, Entry{pos, {}});
    for (int index = 0; index < std::min(slotCount, QcCampfireDefinition::maxSlots); ++index) {
        Slot &slot = entry.places[static_cast<std::size_t>(index)];
        if (slot.stack.isEmpty()) {
            slot = {QcItemStack{item, 1, 0}, 0.0};
            set(entry);
            return true;
        }
    }
    return false;
}

void QcCampfires::set(const Entry &entry)
{
    const auto it = m_entries.find(entry.pos);
    const bool changed = it == m_entries.end() ? !entry.isEmpty()
                                               : !std::ranges::equal(it->places, entry.places, {}, &Slot::stack, &Slot::stack);
    if (changed) {
        m_changes.append(entry.pos);
    }
    if (entry.isEmpty()) {
        remove(entry.pos);
        return;
    }
    if (it == m_entries.end()) {
        m_byChunk[entry.pos.chunk()].append(entry.pos);
    }
    m_entries.insert(entry.pos, entry);
}

void QcCampfires::addCookingTime(QcBlockPos pos, double seconds)
{
    const auto it = m_entries.find(pos);
    if (it == m_entries.end()) {
        return;
    }
    for (Slot &slot : it->places) {
        slot.cooked += slot.stack.isEmpty() ? 0.0 : seconds;
    }
}

const QcCampfires::Entry *QcCampfires::at(QcBlockPos pos) const
{
    const auto it = m_entries.constFind(pos);
    return it != m_entries.cend() ? &*it : nullptr;
}

QList<QcBlockPos> QcCampfires::positions() const
{
    QList<QcBlockPos> positions = m_entries.keys();
    std::ranges::sort(positions);
    return positions;
}

QList<QcBlockPos> QcCampfires::takeChanges()
{
    QList<QcBlockPos> changes = std::move(m_changes);
    m_changes.clear();
    std::ranges::sort(changes);
    const auto [first, last] = std::ranges::unique(changes);
    changes.erase(first, last);
    return changes;
}

QList<QcCampfires::Spill> QcCampfires::takeSpills()
{
    QList<Spill> spills = std::move(m_spills);
    m_spills.clear();
    return spills;
}

void QcCampfires::clear()
{
    m_entries.clear();
    m_byChunk.clear();
    m_changes.clear();
    m_spills.clear();
}

void QcCampfires::remove(QcBlockPos pos)
{
    if (!m_entries.remove(pos)) {
        return;
    }
    const auto chunk = m_byChunk.find(pos.chunk());
    if (chunk != m_byChunk.end()) {
        chunk->removeOne(pos);
        if (chunk->isEmpty()) {
            m_byChunk.erase(chunk);
        }
    }
}
