// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>

#include <ranges>

/*! \brief A read-only view of the definitions (of the type \a T) of a content registry list,
    indexed by their strong id type \a Id instead of an integer (decision E-952).

    \a Id has an explicit constructor from \c {int} and \c {index()}. The view holds a pointer
    to the list: it is valid as long as the registry it came from. Iterating it yields the
    definitions in registration order; ids() yields their ids. */
template<typename Id, typename T>
class QcIdList
{
public:
    /*! Constructs a view of \a items. */
    explicit QcIdList(const QList<T> &items)
        : m_items(&items)
    {
    }
    /*! A view of a temporary list would dangle. */
    QcIdList(const QList<T> &&) = delete;

    /*! Returns the definition of \a id; \a id must be in the list (contains()). */
    [[nodiscard]] const T &operator[](Id id) const
    {
        Q_ASSERT(contains(id));
        return (*m_items)[id.index()];
    }

    /*! Returns the definition of \a id; \a id must be in the list (contains()). */
    [[nodiscard]] const T &at(Id id) const { return (*this)[id]; }

    /*! Returns whether \a id names a definition of the list. */
    [[nodiscard]] bool contains(Id id) const { return id.index() >= 0 && id.index() < m_items->size(); }

    /*! Returns the number of definitions. */
    [[nodiscard]] qsizetype size() const { return m_items->size(); }

    /*! Returns whether the list holds no definition. */
    [[nodiscard]] bool isEmpty() const { return m_items->isEmpty(); }

    /*! Returns an iterator to the first definition. */
    [[nodiscard]] QList<T>::const_iterator begin() const { return m_items->cbegin(); }

    /*! Returns the iterator past the last definition. */
    [[nodiscard]] QList<T>::const_iterator end() const { return m_items->cend(); }

    /*! Returns the ids of all definitions in registration order. */
    [[nodiscard]] auto ids() const
    {
        return std::views::iota(0, static_cast<int>(m_items->size())) | std::views::transform([](int index) { return Id{index}; });
    }

    /*! Returns the id of the definition \a it points to (an iterator of this list). */
    [[nodiscard]] Id idOf(QList<T>::const_iterator it) const { return Id{static_cast<int>(it - m_items->cbegin())}; }

    /*! Returns the underlying list, for code that hands it on unchanged. */
    [[nodiscard]] const QList<T> &list() const { return *m_items; }

private:
    const QList<T> *m_items;
};
