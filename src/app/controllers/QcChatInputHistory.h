// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <utility>

// The lines the player sent in the chat line, for arrow up and down as in a shell (decision E-970;
// GameController.hud.chat.inputs): previous() steps back through them, next() forward again, and
// stepping past the newest line brings back the draft: the text typed before the first step, or a
// recalled line the player changed before stepping on (the latest such change; stored lines stay
// as sent). The lines live for the session only, at most `limit`, without empty lines and without
// the same line twice in a row.
class QcChatInputHistory : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ChatInputHistory)
    QML_UNCREATABLE("Provided by GameController.hud.chat.inputs")

public:
    static constexpr qsizetype limit = 100;

    explicit QcChatInputHistory(QObject *parent = nullptr);

    // The line before the one shown (`currentText`, kept as the draft on the first step back or
    // when it is a changed recalled line); `currentText` again at the oldest line or without lines.
    Q_INVOKABLE [[nodiscard]] QString previous(const QString &currentText);
    // The line after the one shown (`currentText` kept as the draft when it is a changed recalled
    // line): past the newest line the draft; `currentText` again while no line is recalled.
    Q_INVOKABLE [[nodiscard]] QString next(const QString &currentText);
    // Stores a sent `line` (trimmed, as the session sends it) as the newest one, unless it is empty
    // or the same as the newest; the next step back starts from the newest line again.
    Q_INVOKABLE void remember(const QString &line);
    // The next step back starts from the newest line and keeps a new draft (the chat line opened
    // or closed).
    Q_INVOKABLE void resetCursor();

    // The stored lines, oldest first.
    [[nodiscard]] const QList<QString> &lines() const { return m_lines; }

private:
    // Index of the recalled line; lines().size() while the draft shows.
    [[nodiscard]] qsizetype cursor() const { return m_cursor; }
    void setCursor(qsizetype cursor) { m_cursor = cursor; }
    void setLines(QList<QString> lines) { m_lines = std::move(lines); }
    // The text typed before the first step back, shown again past the newest line.
    [[nodiscard]] const QString &draft() const { return m_draft; }
    void setDraft(const QString &draft) { m_draft = draft; }
    // Keeps `currentText` as the draft unless it is the recalled line unchanged.
    void keepAsDraft(const QString &currentText);
    [[nodiscard]] bool recalling() const { return cursor() < lines().size(); }

    QList<QString> m_lines;
    qsizetype m_cursor = 0;
    QString m_draft;
};
