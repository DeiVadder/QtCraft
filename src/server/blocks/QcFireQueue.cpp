#include "server/blocks/QcFireQueue.h"

#include <algorithm>

QcFireQueue::QcFireQueue(std::vector<bool> fire, std::vector<bool> eternalGround)
    : m_fire(std::move(fire))
    , m_eternalGround(std::move(eternalGround))
{
}

void QcFireQueue::noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId block)
{
    if (isEternalGround(before) != isEternalGround(block)) {
        noteGround(pos.offset(0, 1, 0), isEternalGround(block));
    }

    if (isFire(block)) {
        if (!m_fires.contains(pos)) {
            m_unplanned.insert(pos);
        }
        return;
    }
    if (isFire(before)) {
        remove(pos);
        m_unplanned.remove(pos);
    }
}

void QcFireQueue::noteChunk(QcChunkPos pos, const QcChunk &chunk, quint32 sections)
{
    constexpr int size = QcWorld::chunkSize;
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        const QcChunkSection &section = chunk.section(index);
        if ((sections & (1u << unsigned(index))) == 0 || !std::ranges::any_of(section.palette(), [this](QcBlockId id) { return isFire(id); })) {
            continue;
        }

        const QcBlockPos corner{pos.x * size, index * QcWorld::sectionHeight, pos.z * size};
        for (int cell = 0; cell < size * size * QcWorld::sectionHeight; ++cell) {
            const QcBlockPos at = corner.offset(cell % size, cell / (size * size), cell / size % size);
            if (isFire(section.block(at.x - corner.x, at.y - corner.y, at.z - corner.z)) && !m_fires.contains(at)) {
                m_unplanned.insert(at);
            }
        }
    }
}

void QcFireQueue::forgetChunk(QcChunkPos pos)
{
    const QList<Entry> entries = entriesOf(pos);
    for (const Entry &entry : entries) {
        remove(entry.fire.pos);
    }
    m_unplanned.removeIf([pos](QcBlockPos fire) { return fire.chunk() == pos; });
    m_refused.removeIf([pos](QcBlockPos fire) { return fire.chunk() == pos; });
}

bool QcFireQueue::hasRoom(QcChunkPos chunk) const
{
    return counted() < m_limits.total && m_perChunk.value(chunk) < m_limits.perChunk;
}

bool QcFireQueue::add(const Fire &fire)
{
    if (m_fires.contains(fire.pos)) {
        return false;
    }

    const Key key{fire.due, m_order++};
    m_fires.insert(fire.pos, {fire, key});
    m_due.emplace(key, fire.pos);
    m_byChunk[fire.pos.chunk()].insert(fire.pos);
    if (fire.eternal) {
        ++m_eternal;
    } else {
        count(fire.pos, 1);
    }

    m_unplanned.remove(fire.pos);
    m_refused.remove(fire.pos);
    return true;
}

void QcFireQueue::drop(QcBlockPos pos)
{
    remove(pos);
}

std::optional<QcFireQueue::Fire> QcFireQueue::entry(QcBlockPos pos) const
{
    const auto it = m_fires.constFind(pos);
    return it == m_fires.constEnd() ? std::nullopt : std::optional<Fire>(it->fire);
}

std::optional<QcFireQueue::Fire> QcFireQueue::takeDue(quint64 now)
{
    while (hasDue(now)) {
        const QcBlockPos pos = m_due.begin()->second;
        m_due.erase(m_due.begin());
        const auto it = m_fires.find(pos);
        if (it == m_fires.end()) {
            continue;
        }
        it->key = unplanned;
        return it->fire;
    }
    return std::nullopt;
}

bool QcFireQueue::plan(const Fire &fire)
{
    const auto it = m_fires.find(fire.pos);
    if (it == m_fires.end()) {
        return false;
    }
    m_due.erase(it->key);
    const bool eternal = it->fire.eternal;
    it->fire = fire;
    it->fire.eternal = eternal;
    it->key = {fire.due, m_order++};
    m_due.emplace(it->key, fire.pos);
    return true;
}

qsizetype QcFireQueue::dueCount(quint64 now) const
{
    return qsizetype(std::distance(m_due.begin(), m_due.upper_bound(Key{now, ~quint64(0)})));
}

QList<QcBlockPos> QcFireQueue::takeUnplanned()
{
    QList<QcBlockPos> cells(m_unplanned.begin(), m_unplanned.end());
    m_unplanned.clear();
    std::ranges::sort(cells);
    return cells;
}

QList<QcBlockPos> QcFireQueue::takeRefused()
{
    QList<QcBlockPos> refused(m_refused.begin(), m_refused.end());
    m_refused.clear();
    std::ranges::sort(refused);
    return refused;
}

QList<QcFireQueue::Saved> QcFireQueue::takeChunk(QcChunkPos chunk, quint64 now)
{
    QList<Saved> saved = savedOf(chunk, now);
    forgetChunk(chunk);
    return saved;
}

QList<QcFireQueue::Saved> QcFireQueue::savedOf(QcChunkPos chunk, quint64 now) const
{
    QList<Saved> saved;
    const QList<Entry> entries = entriesOf(chunk);
    for (const Entry &entry : entries) {
        const Fire &fire = entry.fire;
        const quint64 left = fire.due > now ? fire.due - now : 0;
        saved.append({fire.pos, fire.origin, fire.age, quint32(std::min<quint64>(left, quint64(1) << 20))});
    }
    return saved;
}

void QcFireQueue::restore(const QList<Saved> &fires, quint64 now, const QcChunk &chunk)
{
    for (const Saved &saved : fires) {
        const QcBlockPos &pos = saved.pos;
        if (pos.chunk() != chunk.pos() || !isFire(chunk.block(pos.localX(), pos.y, pos.localZ()))) {
            continue;
        }

        // A fire without room goes out rather than wait for adoption: adopted, it would start again
        // as a new spark with a new budget (decision E-394).
        const bool eternal = pos.y > 0 && isEternalGround(chunk.block(pos.localX(), pos.y - 1, pos.localZ()));
        if (!eternal && !hasRoom(pos.chunk())) {
            m_unplanned.remove(pos);
            m_refused.insert(pos);
            continue;
        }
        add({pos, saved.origin, quint8(std::min<int>(saved.age, maxAge)), now + saved.ticksLeft, eternal});
    }
}

QList<QcFireQueue::Fire> QcFireQueue::fires() const
{
    QList<Fire> all;
    all.reserve(m_fires.size());
    for (const Entry &entry : m_fires) {
        all.append(entry.fire);
    }
    std::ranges::sort(all, {}, &Fire::pos);
    return all;
}

void QcFireQueue::remove(QcBlockPos pos)
{
    const auto it = m_fires.find(pos);
    if (it == m_fires.end()) {
        return;
    }
    m_due.erase(it->key);
    if (it->fire.eternal) {
        --m_eternal;
    } else {
        count(pos, -1);
    }

    const auto inChunk = m_byChunk.find(pos.chunk());
    if (inChunk != m_byChunk.end()) {
        inChunk->remove(pos);
        if (inChunk->isEmpty()) {
            m_byChunk.erase(inChunk);
        }
    }
    m_fires.erase(it);
}

void QcFireQueue::noteGround(QcBlockPos fire, bool eternal)
{
    const auto it = m_fires.find(fire);
    if (it == m_fires.end() || it->fire.eternal == eternal) {
        return;
    }
    if (eternal) {
        count(fire, -1);
        it->fire.eternal = true;
        ++m_eternal;
        return;
    }

    if (!hasRoom(fire.chunk())) {
        remove(fire);
        m_refused.insert(fire);
        return;
    }
    it->fire.eternal = false;
    --m_eternal;
    count(fire, 1);
}

void QcFireQueue::count(QcBlockPos pos, int by)
{
    const QcChunkPos chunk = pos.chunk();
    qsizetype &inChunk = m_perChunk[chunk];
    inChunk += by;
    if (inChunk <= 0) {
        m_perChunk.remove(chunk);
    }
}

QList<QcFireQueue::Entry> QcFireQueue::entriesOf(QcChunkPos chunk) const
{
    QList<Entry> entries;
    const auto inChunk = m_byChunk.constFind(chunk);
    if (inChunk == m_byChunk.constEnd()) {
        return entries;
    }

    entries.reserve(inChunk->size());
    for (const QcBlockPos &pos : *inChunk) {
        entries.append(m_fires.value(pos));
    }
    std::ranges::sort(entries, {}, &Entry::key);
    return entries;
}
