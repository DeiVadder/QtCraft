// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/handbook/QcHandbookEntry.h"

#include <QHash>
#include <QList>
#include <QString>

/*! \brief The links of a handbook section, one per target: a link to a target that is already
    there adds its note to that one. */
class QcLinkList
{
public:
    /*! A link to an entry. */
    using Link = QcHandbookEntry::Link;

    /*! Appends \a link unless one to the same target exists; that one gets the note added. Links
        without a target are always appended. */
    void addUnique(Link link);

    /*! Returns whether the list holds no link. */
    bool isEmpty() const { return m_links.isEmpty(); }
    /*! Returns the links in the order they were added. */
    const QList<Link> &links() const { return m_links; }
    /*! Returns the links sorted by text (as compared by the search), then by target. */
    [[nodiscard]] QList<Link> sorted() const;

private:
    QList<Link> m_links;
    // Target -> its position in m_links counted from 1; 0 (inserted by the lookup) for a new target.
    QHash<QString, qsizetype> m_positionOfTarget;
};
