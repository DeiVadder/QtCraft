// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/admin/QcChatLog.h"

#include <QDateTime>

void QcChatLog::add(const QString &sender, const QcMsg::ChatMessage &line)
{
    if (m_entries.size() >= limit) {
        m_entries.removeFirst();
    }
    m_entries.append({++m_latest, {QDateTime::currentMSecsSinceEpoch(), sender, line}});
}

QList<QcMsg::AdminChatLine> QcChatLog::since(quint64 after) const
{
    QList<QcMsg::AdminChatLine> lines;
    for (const Entry &entry : m_entries) {
        if (entry.sequence > after) {
            lines.append(entry.line);
        }
    }
    return lines;
}
