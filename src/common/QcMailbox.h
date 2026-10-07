#pragma once

#include <mutex>
#include <utility>
#include <vector>

// Thread-safe hand-over of values from producer threads to one consumer. Uses std::mutex on
// purpose: ThreadSanitizer sees its happens-before edges, unlike Qt's event queue inside the
// uninstrumented QtCore. push() reports whether the box was empty, so the producer wakes the
// consumer only once per batch.
template<typename T>
class QcMailbox
{
public:
    bool push(T value)
    {
        const std::scoped_lock lock(m_mutex);
        const bool wasEmpty = m_items.empty();
        m_items.push_back(std::move(value));
        return wasEmpty;
    }

    std::vector<T> takeAll()
    {
        const std::scoped_lock lock(m_mutex);
        return std::exchange(m_items, {});
    }

private:
    std::mutex m_mutex;
    std::vector<T> m_items;
};
