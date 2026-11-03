// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcChatInputHistory.h"

#include <QList>
#include <QString>

#include <utility>

QcChatInputHistory::QcChatInputHistory(QObject *parent)
    : QObject(parent)
{
}

QString QcChatInputHistory::previous(const QString &currentText)
{
    if (cursor() == 0) {
        return currentText;
    }
    keepAsDraft(currentText);
    setCursor(cursor() - 1);
    return lines().at(cursor());
}

QString QcChatInputHistory::next(const QString &currentText)
{
    if (!recalling()) {
        return currentText;
    }
    keepAsDraft(currentText);
    setCursor(cursor() + 1);
    return recalling() ? lines().at(cursor()) : draft();
}

void QcChatInputHistory::remember(const QString &line)
{
    const QString sent = line.trimmed();
    if (!sent.isEmpty() && (lines().isEmpty() || lines().constLast() != sent)) {
        QList<QString> stored = lines();
        stored.append(sent);
        if (stored.size() > limit) {
            stored.removeFirst();
        }
        setLines(std::move(stored));
    }
    resetCursor();
}

void QcChatInputHistory::keepAsDraft(const QString &currentText)
{
    // The typed draft, or a recalled line the player changed: it comes back past the newest line.
    if (!recalling() || currentText != lines().at(cursor())) {
        setDraft(currentText);
    }
}

void QcChatInputHistory::resetCursor()
{
    setCursor(lines().size());
    setDraft({});
}
