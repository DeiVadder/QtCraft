#include "app/rendering/QcRangeAllocator.h"

#include "app/debug/QcAppLogging.h"

#include <iterator>

QcRangeAllocator::QcRangeAllocator(quint32 capacity)
    : m_capacity(capacity)
{
    if (capacity > 0) {
        m_free.emplace(0, capacity);
    }
}

std::optional<quint32> QcRangeAllocator::allocate(quint32 count)
{
    if (count == 0) {
        return std::nullopt;
    }

    // A page holds a few hundred sections at most, so a linear first fit stays cheap and keeps the
    // used ranges packed towards the start.
    for (auto range = m_free.begin(); range != m_free.end(); ++range) {
        const auto [start, length] = *range;
        if (length < count) {
            continue;
        }
        m_free.erase(range);
        if (length > count) {
            m_free.emplace(start + count, length - count);
        }
        m_used += count;
        return start;
    }
    return std::nullopt;
}

void QcRangeAllocator::release(quint32 start, quint32 count)
{
    if (count == 0) {
        return;
    }
    // A range outside the capacity or more units than are in use is a caller's bug; ignoring it keeps
    // the free list consistent.
    if (static_cast<quint64>(start) + count > m_capacity || count > m_used) {
        qCWarning(lcRenderer).nospace() << "Range " << start << "+" << count << " was not handed out by this allocator";
        return;
    }

    // A range that overlaps a free one was given back twice, or never handed out as a whole: the
    // neighbours in the free list on both sides tell.
    auto next = m_free.lower_bound(start);
    const bool overlapsNext = next != m_free.end() && next->first < start + count;
    const bool overlapsPrevious = next != m_free.begin() && std::prev(next)->first + std::prev(next)->second > start;
    if (overlapsNext || overlapsPrevious) {
        qCWarning(lcRenderer).nospace() << "Range " << start << "+" << count << " is partly free already";
        return;
    }

    m_used -= count;
    quint32 first = start;
    quint32 length = count;
    // Merge with the free range right after it.
    if (next != m_free.end() && next->first == start + count) {
        length += next->second;
        next = m_free.erase(next);
    }
    // Merge with the free range right before it.
    if (next != m_free.begin()) {
        const auto previous = std::prev(next);
        if (previous->first + previous->second == start) {
            first = previous->first;
            length += previous->second;
            m_free.erase(previous);
        }
    }
    m_free.emplace(first, length);
}
