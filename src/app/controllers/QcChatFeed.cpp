// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcChatFeed.h"

#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

QcChatFeed::QcChatFeed(QObject *parent)
    : QObject(parent)
    , m_inputs(this)
{
    m_elapsed.start();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &QcChatFeed::refresh);
}

bool QcChatFeed::isCommand(const QString &line)
{
    return line.startsWith(u'/');
}

bool QcChatFeed::showsHelpHint(const QString &line, bool commandSent)
{
    if (line.isEmpty()) {
        return !commandSent;
    }
    return isCommand(line) && !line.contains(u' ');
}

void QcChatFeed::add(const QString &text)
{
    QList<Line> lines = history();
    lines.append({text, now()});
    while (lines.size() > historyLines) {
        lines.removeFirst();
    }
    setHistory(std::move(lines));
    emit entriesChanged();
    schedule();
}

void QcChatFeed::clear()
{
    if (history().isEmpty()) {
        return;
    }
    setHistory({});
    schedule();
    emit entriesChanged();
}

void QcChatFeed::setOpen(bool open)
{
    // Main.qml binds it: the same value again changes nothing.
    if (open == isOpen()) {
        return;
    }
    m_open = open;
    m_inputs.resetCursor();
    emit openChanged();
    emit entriesChanged();
    schedule();
}

QList<QcChatFeed::Entry> QcChatFeed::shown() const
{
    QList<Entry> lines;
    const qint64 time = now();
    const qsizetype count = std::min(history().size(), isOpen() ? openLines : closedLines);
    for (qsizetype i = history().size() - count; i < history().size(); ++i) {
        const Line &line = history()[i];
        if (isOpen()) {
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
    setNextRefresh(std::nullopt);
    if (isOpen()) {
        return;
    }

    // The next start or end of a fade among the lines that show while closed.
    const qint64 time = now();
    const qsizetype first = std::max<qsizetype>(0, history().size() - closedLines);
    std::optional<qint64> next;
    for (qsizetype i = first; i < history().size(); ++i) {
        const qint64 fadeStart = history()[i].at + fadeAfterMs;
        for (const qint64 event : {fadeStart, fadeStart + fadeMs}) {
            if (event > time && (!next || event - time < *next)) {
                next = event - time;
            }
        }
    }
    setNextRefresh(next);
    if (next) {
        m_timer.start(static_cast<int>(*next));
    }
}
