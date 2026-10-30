// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcLogging.h"

#include <QHash>
#include <QList>
#include <QString>

#include <algorithm>
#include <optional>
#include <utility>

/*! \brief Definitions of one kind in registration order, found by their \c {name}: registering a
    name again replaces the earlier definition in its place, so a later pack overrides an earlier
    one. */
template<typename Definition>
class QcNamedList
{
public:
    /*! Replaces the definition with the same name as \a definition, or appends it. */
    void set(Definition definition)
    {
        const auto existing = m_index.constFind(definition.name);
        if (existing != m_index.constEnd()) {
            m_items[*existing] = std::move(definition);
            return;
        }

        m_index.insert(definition.name, m_items.size());
        m_items.append(std::move(definition));
    }

    /*! Removes the definition \a name; the later ones move up. */
    void remove(const QString &name)
    {
        const auto it = m_index.constFind(name);
        if (it == m_index.constEnd()) {
            return;
        }

        m_items.removeAt(*it);
        m_index.clear();
        for (qsizetype index = 0; index < m_items.size(); ++index) {
            m_index.insert(m_items[index].name, index);
        }
    }

    /*! Keeps the first \a count definitions and forgets the later ones. */
    void truncate(qsizetype count)
    {
        for (qsizetype index = count; index < m_items.size(); ++index) {
            m_index.remove(m_items[index].name);
        }
        m_items.resize(std::min(count, m_items.size()));
    }
    /*! Returns the position of the definition \a name in items(); none if there is none. */
    [[nodiscard]] std::optional<qsizetype> indexOf(const QString &name) const
    {
        const auto it = m_index.constFind(name);
        return it == m_index.constEnd() ? std::nullopt : std::optional<qsizetype>(*it);
    }

    /*! Returns the definition \a name, or null if there is none. */
    [[nodiscard]] const Definition *find(const QString &name) const
    {
        const auto it = m_index.constFind(name);
        return it == m_index.constEnd() ? nullptr : &m_items[*it];
    }

    /*! Returns the definitions in registration order. */
    [[nodiscard]] const QList<Definition> &items() const { return m_items; }

    /*! Changes the definition at \a index by calling \a change with it
        (\c {change(Definition &)}). The name finds the definition, so it stays: a change of it is
        undone with a warning. */
    template<typename Change>
    void update(qsizetype index, Change &&change)
    {
        Definition &definition = m_items[index];
        const QString name = definition.name;
        std::forward<Change>(change)(definition);
        if (definition.name != name) {
            qCWarning(lcRegistry, "Renaming '%s' to '%s' in place is refused; register it under the new name instead", qPrintable(name),
                      qPrintable(definition.name));
            definition.name = name;
        }
    }

    /*! Calls update() with \a change for every definition in order. */
    template<typename Change>
    void updateAll(const Change &change)
    {
        for (qsizetype index = 0; index < m_items.size(); ++index) {
            update(index, change);
        }
    }

private:
    QList<Definition> m_items;
    QHash<QString, qsizetype> m_index;
};
