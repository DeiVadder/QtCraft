// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/blockrules/QcPortalShapes.h"
#include "common/core/QcResult.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

/*! \brief The lit portals a dimension knows (decision E-1023): each with its kind and frame, kept
    in portals.json in the directory of the dimension.

    A scaled link looks here for a portal near its point instead of loading the chunks around it,
    and creatures, items and minecarts are only looked at inside these frames. A portal joins when
    it is lit, made at an arrival or used; one whose block went away leaves when it is looked at
    (forgetGone()). Lives in the server's thread with its QcServerDimension. */
class QcPortalIndex
{
public:
    /*! The most portals a dimension keeps; a new one beyond replaces the oldest. */
    static constexpr qsizetype maxPortals = 4096;
    /*! The file name in the directory of the dimension. */
    static constexpr auto fileName = "portals.json";

    /*! \brief A lit portal: its kind (QcPortalDefinition::name) and its frame. \qcfields */
    struct Entry
    {
        QString portal;
        QcPortalFrame frame;
        bool operator==(const Entry &) const = default;
    };

    /*! Adds \a entry unless it is known; returns whether it was new. */
    bool add(const Entry &entry);
    /*! Takes \a entry out; returns whether it was known. */
    bool remove(const Entry &entry);
    /*! Returns the portals, the oldest first. */
    const QList<Entry> &entries() const { return m_entries; }
    /*! Returns the portals of the kind \a portal whose middle lies within \a radius blocks of
        \a point along x and along z, the nearest first (in three dimensions). */
    [[nodiscard]] QList<Entry> near(const QString &portal, const QcVec3 &point, double radius) const;
    /*! Returns the portal whose inside holds \a cell; none when no known portal does. */
    [[nodiscard]] std::optional<Entry> holding(QcBlockPos cell) const;
    /*! Takes out the portals of which \a isGone says their block went away; returns how many. */
    qsizetype forgetGone(const std::function<bool(const Entry &)> &isGone);
    /*! Returns how often the portals changed since the start, for whether to save them. */
    quint64 changes() const { return m_changes; }

    /*! Replaces the portals with \a entries (a dimension that opens), the first maxPortals. */
    void restore(const QList<Entry> &entries);
    /*! Returns the contents of portals.json. */
    [[nodiscard]] QByteArray bytes() const;
    /*! Returns the portals of the portals.json \a bytes; entries that are not plausible are left
        out. An error for a file that is no JSON object of this kind. */
    [[nodiscard]] static QcResult<QList<Entry>> fromBytes(const QByteArray &bytes);
    /*! Returns the portals.json in \a directory; empty without error when there is none. */
    [[nodiscard]] static QcResult<QList<Entry>> read(const QString &directory);

private:
    void changed() { ++m_changes; }

    QList<Entry> m_entries;
    quint64 m_changes = 0;
};
