// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHashFunctions>

#include <compare>
#include <cstddef>

/*! \brief The index of a definition in a list of its own kind, as a type of its own per list
    (decision E-952): \a Tag tells the lists apart, so an index of one list cannot pass for one
    of another.

    It converts neither from nor to an integer by itself: codecs write index() as the integer of
    the format and construct the id from the integer they read. A default-constructed id is the
    first entry (index 0). Ids order and hash by their index; QcIdList indexes a list by them. */
template<typename Tag>
class QcIndexId
{
public:
    /*! Constructs the id of the first entry (index 0). */
    constexpr QcIndexId() = default;

    /*! Constructs the id of the entry with the index \a index. */
    constexpr explicit QcIndexId(int index)
        : m_index(index)
    {
    }

    /*! Returns the index of the entry in its list. */
    [[nodiscard]] constexpr int index() const { return m_index; }

    /*! Returns whether \a a and \a b name the same entry. */
    friend constexpr bool operator==(QcIndexId a, QcIndexId b) = default;
    /*! Returns the order of \a a and \a b by their index. */
    friend constexpr std::strong_ordering operator<=>(QcIndexId a, QcIndexId b) = default;

    /*! Returns the hash of \a id with \a seed, for QHash and QSet. */
    friend std::size_t qHash(QcIndexId id, std::size_t seed = 0) noexcept { return qHash(id.m_index, seed); }

private:
    int m_index = 0;
};
