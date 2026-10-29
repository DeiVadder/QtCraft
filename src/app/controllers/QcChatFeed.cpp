// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcChatFeed.h"

#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

QcChatFeed::QcChatFeed(QObject *parent)
    : QObject(parent)
{
    m_elapsed.start();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &QcChatFeed::refresh);
}

void QcChatFeed::add(const QString &text)
{
    m_history.append({text, now()});
    while (m_history.size() > historyLines) {
        m_history.removeFirst();
    }
    emit entriesChanged();
    schedule();
}

void QcChatFeed::clear()
{
    if (m_history.isEmpty()) {
        return;
    }
    m_history.clear();
    schedule();
    emit entriesChanged();
}

void QcChatFeed::setOpen(bool open)
{
    // Main.qml binds it: the same value again changes nothing.
    if (open == m_open) {
        return;
    }
    m_open = open;
    emit openChanged();
    emit entriesChanged();
    schedule();
}

QList<QcChatFeed::Entry> QcChatFeed::shown() const
{
    QList<Entry> lines;
    const qint64 time = now();
    const qsizetype count = std::min(m_history.size(), m_open ? openLines : closedLines);
    for (qsizetype i = m_history.size() - count; i < m_history.size(); ++i) {
        const Line &line = m_history[i];
        if (m_open) {
            lines.append({line.text, 1.0});
            continue;
        }
        const qint64 left = line.at + fadeAfterMs + fadeMs - time;
        if (left <= 0) {
            continue;
        }
        const bool fading = left <= fadeMs;
        lines.append({line.text, std::min(1.0, static_cast<double>(left) / static_cast<double>(fadeMs)), fading ? left : 0});
    }
    return lines;
}

QVariantList QcChatFeed::entries() const
{
    QVariantList list;
    const QList<Entry> lines = shown();
    for (const Entry &entry : lines) {
        list.append(QVariantMap{{u"text"_s, entry.text}, {u"opacity"_s, entry.opacity}, {u"fadeMs"_s, entry.fadeMs}});
    }
    return list;
}

void QcChatFeed::setClock(std::function<qint64()> clock)
{
    m_clock = std::move(clock);
}

void QcChatFeed::refresh()
{
    emit entriesChanged();
    schedule();
}

void QcChatFeed::schedule()
{
    m_timer.stop();
    m_nextRefresh.reset();
    if (m_open) {
        return;
    }

    // The next start or end of a fade among the lines that show while closed.
    const qint64 time = now();
    const qsizetype first = std::max<qsizetype>(0, m_history.size() - closedLines);
    for (qsizetype i = first; i < m_history.size(); ++i) {
        const qint64 fadeStart = m_history[i].at + fadeAfterMs;
        for (const qint64 event : {fadeStart, fadeStart + fadeMs}) {
            if (event > time && (!m_nextRefresh || event - time < *m_nextRefresh)) {
                m_nextRefresh = event - time;
            }
        }
    }
    if (m_nextRefresh) {
        m_timer.start(static_cast<int>(*m_nextRefresh));
    }
}
