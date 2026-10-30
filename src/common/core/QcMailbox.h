// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <mutex>
#include <utility>
#include <vector>

/*! \brief Thread-safe hand-over of values from producer threads to one consumer.

    Uses std::mutex on purpose: ThreadSanitizer sees its happens-before edges, unlike Qt's event
    queue inside the uninstrumented QtCore. push() reports whether the box was empty, so the
    producer wakes the consumer only once per batch. */
template<typename T>
class QcMailbox
{
public:
    /*! Adds \a value. Returns whether the box was empty before. */
    bool push(T value)
    {
        const std::scoped_lock lock(m_mutex);
        const bool wasEmpty = m_items.empty();
        m_items.push_back(std::move(value));
        return wasEmpty;
    }

    /*! Returns all values in the order they came and empties the box. */
    std::vector<T> takeAll()
    {
        const std::scoped_lock lock(m_mutex);
        return std::exchange(m_items, {});
    }

private:
    std::mutex m_mutex;
    std::vector<T> m_items;
};
