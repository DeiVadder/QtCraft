// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcAdminMessages.h"

#include <QList>

/*! \brief The latest chat lines everyone on the server saw (decision E-848): players' chat and
    /say, with when and from whom, numbered in the order they came, for the admin channel's chat
    view. */
class QcChatLog
{
public:
    /*! Lines kept at most; the oldest goes first. */
    static constexpr qsizetype limit = 200;

    /*! Adds \a line from \a sender with the next number; \a sender is empty for lines of the
        server. */
    void add(const QString &sender, const QcMsg::ChatMessage &line);
    /*! Returns the lines after the one numbered \a after, oldest first (at most \c {limit}). */
    QList<QcMsg::AdminChatLine> since(quint64 after) const;
    /*! Returns the number of the newest line; 0 before the first. */
    quint64 latest() const { return m_latest; }

private:
    struct Entry
    {
        quint64 sequence = 0;
        QcMsg::AdminChatLine line;
    };

    QList<Entry> m_entries;
    quint64 m_latest = 0;
};
