#pragma once

#include "common/protocol/QcAdminMessages.h"

#include <QList>

// The latest chat lines everyone on the server saw (decision E-848): players' chat and /say, with
// when and from whom, numbered in the order they came, for the admin channel's chat view.
class QcChatLog
{
public:
    static constexpr qsizetype limit = 200;

    // `sender` is empty for lines of the server.
    void add(const QString &sender, const QcMsg::ChatMessage &line);
    // The lines after the one numbered `after`, oldest first (at most `limit`).
    QList<QcMsg::AdminChatLine> since(quint64 after) const;
    // The number of the newest line; 0 before the first.
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
