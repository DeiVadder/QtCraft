// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

class QcServerDimension;

/*! \brief What a part of the server keeps per dimension (decision E-988): a \a T for each dimension
    asked about, made when first asked for.

    A part that exists once for the whole server keeps here what belongs to the blocks and bodies
    of a dimension (queues, positions), and asks with the current dimension of QcServerState.
    Lives in the server's thread. */
template<typename T>
class QcPerDimension
{
public:
    /*! Makes the T of a dimension. */
    using Maker = std::function<std::unique_ptr<T>()>;

    /*! Makes a T with its default constructor. */
    QcPerDimension()
        : m_make([] { return std::make_unique<T>(); })
    {
    }
    /*! Makes a T with \a make. */
    explicit QcPerDimension(Maker make)
        : m_make(std::move(make))
    {
    }

    /*! Returns the T of \a dimension; the reference stays valid. */
    T &of(const QcServerDimension &dimension) const
    {
        if (&dimension != m_lastDimension) {
            std::unique_ptr<T> &entry = m_entries[&dimension];
            if (!entry) {
                entry = m_make();
            }
            m_lastDimension = &dimension;
            m_last = entry.get();
        }
        return *m_last;
    }

private:
    Maker m_make;
    // Asked for from const functions too: the entries appear on demand, what they hold does not change.
    mutable std::unordered_map<const QcServerDimension *, std::unique_ptr<T>> m_entries;
    mutable const QcServerDimension *m_lastDimension = nullptr;
    mutable T *m_last = nullptr;
};
